#include <string.h>
#include <stdio.h>
#include "common.h"
#include "parser.h"
#include "options.h"
#include "memory.h"
#include "debug.h"
#include "vm.h"

#ifdef NDEBUG
#define TRACE_START(name) (void)0
#define TRACE_END(name)   (void)0
#else
#define TRACE_START(name) _trace_start(name)
#define TRACE_END(name) _trace_end(name)
#endif

static void _trace_start(const char *name) {
    if (CLOX_OPTION_T(traceParserCalls)) {
        fprintf(stderr, "[-- <%s> --]\n", name);
    }
}
static void _trace_end(const char *name) {
    if (CLOX_OPTION_T(traceParserCalls)) {
        fprintf(stderr, "[-- </%s> --]\n", name);
    }
}

// global
Parser parser; // TODO: remove global parser
static Parser *current = NULL;

// initialize/reinitialize parser
void initParser(Parser *p) {
    p->hadError = false;
    p->panicMode = false;
    memset(&p->errJmpBuf, 0, sizeof(jmp_buf));
    memset(&p->current, 0, sizeof(Token));
    memset(&p->previous, 0, sizeof(Token));
    vec_init(&p->peekBuf);
    vec_init(&p->v_errMessages);
    p->inCallExpr = false;
}

void freeParser(Parser *p) {
    vec_clear(&p->v_errMessages);
    vec_clear(&p->peekBuf);
    initParser(p);
}

static void errorAt(Token *token, const char *message) {
  if (current->panicMode) return;
  current->panicMode = true;

  ObjString *str = hiddenString("", 0);
  pushCStringFmt(str, "[Parse Error], (line %d) Error", token->line);

  if (token->type == TOKEN_EOF) {
    pushCStringFmt(str, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing.
  } else {
    pushCStringFmt(str, " at '%.*s'", token->length, token->start);
  }

  pushCStringFmt(str, ": %s\n", message);
  vec_push(&current->v_errMessages, str);
  current->hadError = true;
  longjmp(current->errJmpBuf, JUMP_PERFORMED);
}

static void error(const char *message) {
  errorAt(&current->previous, message);
}
static void errorAtCurrent(const char *message) {
  errorAt(&current->current, message);
}

void outputParserErrors(Parser *p, FILE *f) {
    ASSERT(p);
    ObjString *msg = NULL;
    int i = 0;
    vec_foreach(&p->v_errMessages, msg, i) {
        fprintf(f, "%s", msg->chars);
    }
}

// takes into account peekbuffer, which scanToken() doesn't.
// NOTE: if looking to peek forward for a token, call `peekTokN`
// instead of this function.
static Token nextToken() {
    if (current->peekBuf.length > 0) {
        Token val = vec_first(&current->peekBuf);
        vec_splice(&current->peekBuf, 0, 1);
        return val;
    } else {
        return scanToken();
    }
}

// Fill `parser.current` with next token in stream
static void advance() {
  current->previous = current->current;

  // parse the next non-error token
  for (;;) {
    current->current = nextToken();
    if (current->current.type != TOKEN_ERROR) break;

    errorAtCurrent(current->current.start);
  }
}

// Expect current token to be of given type, otherwise error with given
// message.
static void consume(TokenType type, const char *message) {
  if (current->current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

// Check that the current token is of given type
static bool check(TokenType type) {
  return current->current.type == type;
}

// peekTokN(1) gives token that nextToken() will return on next call
static Token peekTokN(int n) {
    ASSERT(n > 0);
    if (current->peekBuf.length < n) {
        for (int i = current->peekBuf.length; i < n; i++) {
            Token tok = scanToken();
            vec_push(&current->peekBuf, tok);
            if (tok.type == TOKEN_EOF) break;
        }
        return vec_last(&current->peekBuf);
    } else {
        return current->peekBuf.data[n-1];
    }
}

// If current token is of given type, advance to next.
// Otherwise returns false.
static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

// forward decls
static Node *declaration(void);
static Node *varDeclaration(void);
static Node *funDeclaration(ParseFunctionType);
static Node *expression(void);
static Node *assignment(void);
static Node *logicOr(void);
static Node *logicAnd(void);
static Node *equality(void);
static Node *comparison(void);
static Node *addition(void);
static Node *byteManip(void);
static Node *multiplication(void);
static Node *unary(void);
static Node *call(void);
static Node *primary(void);
static Node *blockStmts(void);
static Node *classOrModuleBody(const char *name);
static Node *statement(void);
static Node *printStatement(void);
static Node *blockStatements(void);
static Node *expressionStatement(void);

Node *parseExpression(Parser *p) {
    Parser *oldCurrent = current;
    current = p;
    advance(); // prime parser with parser.current
    TRACE_START("parseExpression");
    Node *ret = expression();
    TRACE_END("parseExpression");
    current = oldCurrent;
    return ret;
}

static bool isAtEnd(void) {
    return current->previous.type == TOKEN_EOF || check(TOKEN_EOF);
}

// returns program node that contains list of statement nodes
// initScanner(char *src) must be called so the scanner is ready
// to pass us the tokens to parse.
Node *parse(Parser *p) {
    if (!vm.inited) {
        // if any errors occur, we add ObjStrings to v_errMessages. Object
        // creation requires VM initialization
        fprintf(stderr, "VM must be initialized (initVM()) before call to parse()\n");
        return NULL;
    }
    initParser(p);
    Parser *oldCurrent = current;
    current = p;
    node_type_t nType = {
        .type = NODE_STMT,
        .kind = STMTLIST_STMT,
    };
    Node *ret = createNode(nType, emptyTok(), NULL);
    advance(); // prime parser with parser.current
    TRACE_START("parse");
    int jumpRes = setjmp(current->errJmpBuf);
    if (jumpRes == JUMP_PERFORMED) { // jumped, had error
        ASSERT(current->panicMode);
        current = oldCurrent;
        TRACE_END("parse (error)");
        return NULL;
    }
    while (!isAtEnd()) {
        Node *stmt = declaration();
        ASSERT(stmt->type.type == NODE_STMT);
        nodeAddChild(ret, stmt);
    }
    TRACE_END("parse");

    if (CLOX_OPTION_T(printAST)) {
        char *output = outputASTString(ret, 0);
        fprintf(stdout, "%s", output);
    }
    current = oldCurrent;
    return ret;
}

static Node *declaration(void) {
    TRACE_START("declaration");
    if (match(TOKEN_VAR)) {
        Node *ret = varDeclaration();
        TRACE_END("declaration");
        return ret;
    }
    if (check(TOKEN_FUN) && peekTokN(1).type == TOKEN_IDENTIFIER) {
        advance();
        Node *ret = funDeclaration(FUNCTION_TYPE_NAMED);
        TRACE_END("declaration");
        return ret;
    }
    if (match(TOKEN_CLASS)) {
        consume(TOKEN_IDENTIFIER, "Expected class name (identifier) after keyword 'class'");
        Token nameTok = current->previous;
        node_type_t classType = {
            .type = NODE_STMT,
            .kind = CLASS_STMT,
        };
        Node *classNode = createNode(classType, nameTok, NULL);
        if (match(TOKEN_LESS)) {
            consume(TOKEN_IDENTIFIER, "Expected class name after '<' in class declaration");
            Token superName = current->previous;
            nodeAddData(classNode, (void*)copyToken(&superName));
        }

        consume(TOKEN_LEFT_BRACE, "Expected '{' after class name");
        Node *body = classOrModuleBody("classBody"); // block node
        nodeAddChild(classNode, body);
        TRACE_END("declaration");
        return classNode;
    }
    if (match(TOKEN_MODULE)) {
        consume(TOKEN_IDENTIFIER, "Expected module name (identifier) after keyword 'module'");
        Token nameTok = current->previous;
        node_type_t modType = {
            .type = NODE_STMT,
            .kind = MODULE_STMT,
        };
        Node *modNode = createNode(modType, nameTok, NULL);
        consume(TOKEN_LEFT_BRACE, "Expected '{' after module name");
        Node *body = classOrModuleBody("moduleBody"); // block node
        nodeAddChild(modNode, body);
        TRACE_END("declaration");
        return modNode;
    }
    Node *ret = statement();
    TRACE_END("declaration");
    return ret;
}

static Node *wrapStmtsInBlock(Node *stmtList, Token lbraceTok) {
    ASSERT(nodeKind(stmtList) == STMTLIST_STMT);
    node_type_t blockType = {
        .type = NODE_STMT,
        .kind = BLOCK_STMT,
    };
    Node *ret = createNode(blockType, lbraceTok, NULL);
    nodeAddChild(ret, stmtList);
    return ret;
}

static Node *statement() {
    TRACE_START("statement");
    if (match(TOKEN_PRINT)) {
        Node *ret = printStatement();
        TRACE_END("statement");
        return ret;
    }
    if (match(TOKEN_LEFT_BRACE)) {
        Node *stmtList = blockStatements();
        node_type_t blockType = {
            .type = NODE_STMT,
            .kind = BLOCK_STMT,
        };
        Node *block = createNode(blockType, current->previous, NULL);
        nodeAddChild(block, stmtList);
        TRACE_END("statement");
        return block;
    }
    if (match(TOKEN_FOREACH)) {
        Token foreachTok = current->previous;
        node_type_t foreachT = {
            .type = NODE_STMT,
            .kind = FOREACH_STMT
        };
        Node *foreachNode = createNode(foreachT, foreachTok, NULL);
        consume(TOKEN_LEFT_PAREN, "Expect '(' after keyword 'foreach'");
        node_type_t varTokT = {
            .type = NODE_OTHER,
            .kind = TOKEN_NODE
        };
        while (match(TOKEN_IDENTIFIER)) {
            Token varTok = current->previous;
            Node *varNode = createNode(varTokT, varTok, NULL);
            nodeAddChild(foreachNode, varNode);
            if (match(TOKEN_IN)) {
                break;
            } else if (match(TOKEN_COMMA)) { // continue
            } else {
                errorAtCurrent("Unexpected token in foreach statement");
            }
        }
        nodeAddChild(foreachNode, expression());
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after 'foreach' statement variables");
        consume(TOKEN_LEFT_BRACE, "Expect '{' after 'foreach' statement variables");
        Token lbraceTok = current->previous;
        Node *foreachStmtList = blockStatements();
        Node *foreachBlock = wrapStmtsInBlock(foreachStmtList, lbraceTok);
        nodeAddChild(foreachNode, foreachBlock);
        TRACE_END("statement");
        return foreachNode;
    }
    if (match(TOKEN_IF)) {
        Token ifTok = current->previous;
        consume(TOKEN_LEFT_PAREN, "Expected '(' after keyword 'if'");
        node_type_t ifType = {
            .type = NODE_STMT,
            .kind = IF_STMT,
        };
        Node *ifNode = createNode(ifType, ifTok, NULL);
        Node *cond = expression();
        consume(TOKEN_RIGHT_PAREN, "Expected ')' to end 'if' condition");
        consume(TOKEN_LEFT_BRACE, "Expected '{' after 'if' condition");
        Token lbraceTok = current->previous;
        nodeAddChild(ifNode, cond);
        Node *ifStmtList = blockStatements();
        Node *ifBlock = wrapStmtsInBlock(ifStmtList, lbraceTok);

        nodeAddChild(ifNode, ifBlock);

        if (match(TOKEN_ELSE)) {
            Node *elseStmt = statement();
            nodeAddChild(ifNode, elseStmt);
            // NOTE: for now, no elseifs
        }
        TRACE_END("statement");
        return ifNode;
    }

    if (match(TOKEN_WHILE)) {
        Token whileTok = current->previous;
        consume(TOKEN_LEFT_PAREN, "Expected '(' after keyword 'while'");
        Node *cond = expression();
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'while' condition");
        Node *blockStmt = statement();
        node_type_t whileT = {
            .type = NODE_STMT,
            .kind = WHILE_STMT,
        };
        Node *whileNode = createNode(whileT, whileTok, NULL);
        nodeAddChild(whileNode, cond);
        nodeAddChild(whileNode, blockStmt);
        TRACE_END("statement");
        return whileNode;
    }

    // for (var i = 0; i < n; i++) { }
    if (match(TOKEN_FOR)) {
        node_type_t forT = {
            .type = NODE_STMT,
            .kind = FOR_STMT,
        };
        Token forTok = current->previous;
        Node *forNode = createNode(forT, forTok, NULL);
        consume(TOKEN_LEFT_PAREN, "Expected '(' after keyword 'for'");
        Node *initializer = NULL; // can be null
        if (match(TOKEN_SEMICOLON)) {
            // leave NULL
        } else {
            if (match(TOKEN_VAR)) {
                initializer = statement();
            } else {
                initializer = expressionStatement();
            }
        }
        nodeAddChild(forNode, initializer);
        Node *expr = NULL;
        if (match(TOKEN_SEMICOLON)) {
            // leave NULL
        } else {
            expr = expression();
            consume(TOKEN_SEMICOLON, "Expected ';' after test expression in 'for'");
        }
        nodeAddChild(forNode, expr);

        Node *incrExpr = NULL;
        if (check(TOKEN_RIGHT_PAREN)) {
            // leave NULL
        } else {
            incrExpr = expression();
        }
        nodeAddChild(forNode, incrExpr);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'for' increment/decrement expression");
        Node *blockNode = statement();
        nodeAddChild(forNode, blockNode);
        TRACE_END("statement");
        return forNode;
    }

    // try { } [catch (Error e) { }]+
    if (match(TOKEN_TRY)) {
        Token tryTok = current->previous;
        node_type_t nType = {
            .type = NODE_STMT,
            .kind = TRY_STMT,
        };
        TRACE_START("tryStatement");
        Node *try = createNode(nType, tryTok, NULL);
        consume(TOKEN_LEFT_BRACE, "Expected '{' after keyword 'try'");
        Token lbraceTok = current->previous;
        Node *stmtList = blockStatements();
        Node *tryBlock = wrapStmtsInBlock(stmtList, lbraceTok);
        nodeAddChild(try, tryBlock);
        while (match(TOKEN_CATCH)) {
            Token catchTok = current->previous;
            consume(TOKEN_LEFT_PAREN, "Expected '(' after keyword 'catch'");
            Node *catchExpr = expression();
            Token identToken;
            bool foundIdentToken = false;
            if (match(TOKEN_IDENTIFIER)) {
                identToken = current->previous;
                foundIdentToken = true;
            }
            consume(TOKEN_RIGHT_PAREN, "Expected ')' to end 'catch' expression");
            consume(TOKEN_LEFT_BRACE, "Expected '{' after 'catch' expression");
            lbraceTok = current->previous;
            Node *catchStmtList = blockStatements();
            Node *catchBlock = wrapStmtsInBlock(catchStmtList, lbraceTok);
            node_type_t catchT = {
                .type = NODE_STMT,
                .kind = CATCH_STMT,
            };
            Node *catchStmt = createNode(catchT, catchTok, NULL);
            nodeAddChild(catchStmt, catchExpr); // class or string or instance, etc.
            if (foundIdentToken) {
                node_type_t varT = {
                    .type = NODE_EXPR,
                    .kind = VARIABLE_EXPR,
                };
                Node *varExpr = createNode(varT, identToken, NULL);
                nodeAddChild(catchStmt, varExpr); // variable to be bound to in block
            }
            nodeAddChild(catchStmt, catchBlock);
            nodeAddChild(try, catchStmt);
        }
        TRACE_END("tryStatement");
        TRACE_END("statement");
        return try;
    }

    if (match(TOKEN_THROW)) {
        Token throwTok = current->previous;
        Node *expr = expression();
        consume(TOKEN_SEMICOLON, "Expected ';' to end 'throw' statement");
        node_type_t throwT = {
            .type = NODE_STMT,
            .kind = THROW_STMT,
        };
        Node *throw = createNode(throwT, throwTok, NULL);
        nodeAddChild(throw, expr);
        TRACE_END("statement");
        return throw;
    }
    if (match(TOKEN_CONTINUE)) {
        Token contTok = current->previous;
        node_type_t contT = {
            .type = NODE_STMT,
            .kind = CONTINUE_STMT,
        };
        Node *cont = createNode(contT, contTok, NULL);
        consume(TOKEN_SEMICOLON, "Expected ';' after keyword 'continue'");
        TRACE_END("statement");
        return cont;
    }
    if (match(TOKEN_BREAK)) {
        Token breakTok = current->previous;
        node_type_t breakT = {
            .type = NODE_STMT,
            .kind = BREAK_STMT,
        };
        Node *breakNode = createNode(breakT, breakTok, NULL);
        consume(TOKEN_SEMICOLON, "Expected ';' after keyword 'break'");
        TRACE_END("statement");
        return breakNode;
    }
    if (match(TOKEN_RETURN)) {
        Token retTok = current->previous;
        node_type_t retT = {
            .type = NODE_STMT,
            .kind = RETURN_STMT,
        };
        Node *retNode = createNode(retT, retTok, NULL);
        if (match(TOKEN_SEMICOLON)) { // do nothing, no child
        } else {
            Node *retExpr = expression();
            nodeAddChild(retNode, retExpr);
            consume(TOKEN_SEMICOLON, "Expected ';' to end 'return' statement");
        }
        TRACE_END("statement");
        return retNode;
    }

    if (match(TOKEN_IN)) {
        Token inTok = current->previous;
        node_type_t inT = {
            .type = NODE_STMT,
            .kind = IN_STMT,
        };
        Node *inNode = createNode(inT, inTok, NULL);
        consume(TOKEN_LEFT_PAREN, "Expected '(' after keyword 'in'");
        Node *expr = expression();
        nodeAddChild(inNode, expr);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'in' expression");
        consume(TOKEN_LEFT_BRACE, "Expected '{' after 'in' expression");
        Node *body = classOrModuleBody("inBody");
        nodeAddChild(inNode, body);
        TRACE_END("statement");
        return inNode;
    }
    Node *ret = expressionStatement();
    TRACE_END("statement");
    return ret;
}

// 'print' is already parsed.
static Node *printStatement() {
    TRACE_START("printStatement");
    node_type_t printType = {
        .type = NODE_STMT,
        .kind = PRINT_STMT,
    };
    Node *printNode = createNode(printType, current->previous, NULL);
    Node *expr = expression();
    consume(TOKEN_SEMICOLON, "Expected ';' after 'print' statement");
    nodeAddChild(printNode, expr);
    TRACE_END("printStatement");
    return printNode;
}

// '{' is already parsed. Parse up until and including the ending '}'
static Node *blockStatements() {
    TRACE_START("blockStatements");
    node_type_t stmtListT = {
        .type = NODE_STMT,
        .kind = STMTLIST_STMT,
    };
    Node *stmtList = createNode(stmtListT, current->previous, NULL);
    while (!isAtEnd() && !check(TOKEN_RIGHT_BRACE)) {
        Node *decl = declaration();
        nodeAddChild(stmtList, decl);
    }
    consume(TOKEN_RIGHT_BRACE, "Expected '}' to end block statement");
    TRACE_END("blockStatements");
    return stmtList;
}

static Node *expressionStatement() {
    TRACE_START("expressionStatement");
    Token tok = current->current;
    Node *expr = expression();
    node_type_t stmtT = {
        .type = NODE_STMT,
        .kind = EXPR_STMT,
    };
    Node *exprStmt = createNode(stmtT, tok, NULL);
    nodeAddChild(exprStmt, expr);
    consume(TOKEN_SEMICOLON, "Expected ';' after expression");
    TRACE_END("expressionStatement");
    return exprStmt;
}

// '{' already parsed. Continue parsing up to and including closing '}'
static Node *classOrModuleBody(const char *debugName) {
    TRACE_START(debugName);
    Token lbraceTok = current->previous;
    node_type_t nType = {
        .type = NODE_STMT,
        .kind = STMTLIST_STMT,
    };
    Node *stmtListNode = createNode(nType, lbraceTok, NULL);
    while (!isAtEnd() && !check(TOKEN_RIGHT_BRACE)) {
        Node *decl;
        if (check(TOKEN_IDENTIFIER) && peekTokN(1).type == TOKEN_LEFT_PAREN) {
            decl = funDeclaration(FUNCTION_TYPE_METHOD);
        } else if (check(TOKEN_IDENTIFIER) && peekTokN(1).type == TOKEN_EQUAL &&
                peekTokN(2).type == TOKEN_LEFT_PAREN) {
            decl = funDeclaration(FUNCTION_TYPE_SETTER);
        } else if (check(TOKEN_IDENTIFIER) && peekTokN(1).type == TOKEN_LEFT_BRACE) {
            decl = funDeclaration(FUNCTION_TYPE_GETTER);
        } else if (check(TOKEN_CLASS) && peekTokN(1).type == TOKEN_IDENTIFIER && peekTokN(2).type == TOKEN_LEFT_PAREN) {
            advance();
            decl = funDeclaration(FUNCTION_TYPE_CLASS_METHOD);
        } else {
            decl = declaration();
        }
        nodeAddChild(stmtListNode, decl);
    }
    consume(TOKEN_RIGHT_BRACE, "Expected '}' to end class/module body");
    node_type_t blockType = {
        .type = NODE_STMT,
        .kind = BLOCK_STMT,
    };
    Node *block = createNode(blockType, lbraceTok, NULL);
    nodeAddChild(block, stmtListNode);
    TRACE_END(debugName);
    return block;
}

// TOKEN_VAR is already consumed.
static Node *varDeclaration(void) {
    TRACE_START("varDeclaration");
    consume(TOKEN_IDENTIFIER, "Expected identifier after keyword 'var'");
    Token identTok = current->previous;
    node_type_t nType = {
        .type = NODE_STMT,
        .kind = VAR_STMT,
    };
    Node *varDecl = createNode(nType, identTok, NULL);
    if (match(TOKEN_EQUAL)) {
        Node *expr = expression();
        nodeAddChild(varDecl, expr);
    }
    consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration");
    TRACE_END("varDeclaration");
    return varDecl;
}

static Node *expression(void) {
    TRACE_START("expression");
    Node *splatCall = NULL;
    if (current->inCallExpr && match(TOKEN_STAR)) {
        node_type_t splatType = {
            .type = NODE_EXPR,
            .kind = SPLAT_EXPR
        };
        splatCall = createNode(splatType, current->previous, NULL);
    }
    Node *expr = assignment();
    if (splatCall != NULL) {
        nodeAddChild(splatCall, expr);
        expr = splatCall;
    }
    TRACE_END("expression");
    return expr;
}

static vec_nodep_t *createNodeVec(void) {
    vec_nodep_t *paramNodes = ALLOCATE(vec_nodep_t, 1);
    ASSERT_MEM(paramNodes);
    vec_init(paramNodes);
    return paramNodes;
}

// FUN keyword has already been parsed, for regular functions
static Node *funDeclaration(ParseFunctionType fnType) {
    TRACE_START("funDeclaration");
    Token nameTok = current->previous;
    if (fnType != FUNCTION_TYPE_ANON) {
        consume(TOKEN_IDENTIFIER, "Expect function name (identifier) after 'fun' keyword");
        nameTok = current->previous;
    }
    if (fnType == FUNCTION_TYPE_SETTER) {
        consume(TOKEN_EQUAL, "Expect '=' after setter method name");
    }
    vec_nodep_t *paramNodes = createNodeVec();
    int numParams = 0;
    int lastParamKind = -1;
    bool inKwargs = false;
    if (fnType != FUNCTION_TYPE_GETTER) {
        consume(TOKEN_LEFT_PAREN, "Expect '(' after function name (identifier)");
        while (true) {
            if (match(TOKEN_IDENTIFIER)) { // regular/default/kwarg param
                Token paramTok = current->previous;
                node_type_t nType = {
                    .type = NODE_OTHER,
                    .kind = PARAM_NODE_REGULAR,
                };
                Node *n = NULL;
                if (match(TOKEN_EQUAL)) { // default argument
                    if (inKwargs) {
                        errorAtCurrent("keyword parameters need to be final parameters");
                    }
                    nType.kind = PARAM_NODE_DEFAULT_ARG;
                    n = createNode(nType, paramTok, NULL);
                    Node *argExpr = expression();
                    nodeAddChild(n, argExpr);
                } else if (match(TOKEN_COLON)) {
                    if (!inKwargs) inKwargs = true;
                    nType.kind = PARAM_NODE_KWARG;
                    n = createNode(nType, paramTok, NULL);
                    if (check(TOKEN_RIGHT_PAREN) || check(TOKEN_COMMA)) { // required keyword arg, no defaults
                        // no children
                    } else {
                        Node *argExpr = expression();
                        nodeAddChild(n, argExpr);
                    }
                } else {
                    if (inKwargs) {
                        errorAtCurrent("keyword parameters need to be final parameters");
                    }
                    n = createNode(nType, paramTok, NULL);
                }
                vec_push(paramNodes, n);
                numParams++;
                lastParamKind = nType.kind;
                if (!match(TOKEN_COMMA)) {
                    break;
                }
            } else if (match(TOKEN_STAR)) { // splat param
                if (inKwargs) {
                    errorAtCurrent("keyword parameters need to be final parameters");
                }
                consume(TOKEN_IDENTIFIER, "Expect splat parameter to have a name");
                Token paramTok = current->previous;
                node_type_t nType = {
                    .type = NODE_OTHER,
                    .kind = PARAM_NODE_SPLAT,
                };
                Node *n = createNode(nType, paramTok, NULL);
                vec_push(paramNodes, n);
                numParams++;
                lastParamKind = nType.kind;
                if (!check(TOKEN_RIGHT_PAREN)) {
                    errorAtCurrent("Expect ')' after rest parameter (must be final parameter)");
                }
            } else {
                break;
            }
        }
        if (fnType == FUNCTION_TYPE_SETTER) {
            if (numParams != 1 || lastParamKind != PARAM_NODE_REGULAR) {
                errorAt(&current->previous, "Expect a single regular parameter for setter function");
            }
        }
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after function parameters");
    }
    consume(TOKEN_LEFT_BRACE, "Expect '{' after function parameter list");
    Token lbrace = current->previous;
    Node *stmtList = blockStmts();
    node_type_t blockType = {
        .type = NODE_STMT,
        .kind = BLOCK_STMT,
    };
    Node *blockNode = createNode(blockType, lbrace, NULL);
    nodeAddChild(blockNode, stmtList);
    node_type_t funcType;
    if (fnType == FUNCTION_TYPE_NAMED) {
        funcType = (node_type_t){
            .type = NODE_STMT,
            .kind = FUNCTION_STMT,
        };
    } else if (fnType == FUNCTION_TYPE_METHOD) {
        funcType = (node_type_t){
            .type = NODE_STMT,
            .kind = METHOD_STMT,
        };
    } else if (fnType == FUNCTION_TYPE_CLASS_METHOD) {
        funcType = (node_type_t){
            .type = NODE_STMT,
            .kind = CLASS_METHOD_STMT,
        };
    } else if (fnType == FUNCTION_TYPE_GETTER) {
        funcType = (node_type_t){
            .type = NODE_STMT,
            .kind = GETTER_STMT,
        };
    } else if (fnType == FUNCTION_TYPE_SETTER) {
        funcType = (node_type_t){
            .type = NODE_STMT,
            .kind = SETTER_STMT,
        };
    } else {
        funcType = (node_type_t){
            .type = NODE_EXPR,
            .kind = ANON_FN_EXPR,
        };
    }
    Node *funcNode = createNode(funcType, nameTok, NULL);
    nodeAddData(funcNode, (void*)paramNodes);
    nodeAddChild(funcNode, blockNode);
    TRACE_END("funDeclaration");
    return funcNode;
}

// a list of stmts
static Node *blockStmts() {
    TRACE_START("blockStmts");
    Token lbraceTok = current->previous;
    node_type_t nType = {
        .type = NODE_STMT,
        .kind = STMTLIST_STMT,
    };
    Node *blockNode = createNode(nType, lbraceTok, NULL);
    while (!isAtEnd() && !check(TOKEN_RIGHT_BRACE)) {
        Node *stmt = declaration();
        nodeAddChild(blockNode, stmt);
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' to end function block");
    TRACE_END("blockStmts");
    return blockNode;
}

static Node *assignment() {
    TRACE_START("assignment");
    Node *lval = logicOr();
    // TODO: support +=, -=, /=, *=
    if (match(TOKEN_EQUAL)) {
        Token eqTok = current->previous;
        Node *rval = assignment(); // assignment goes right to left in precedence (a = (b = c))
        Node *ret = NULL;
        if (nodeKind(lval) == VARIABLE_EXPR) {
            TRACE_START("assignExpr");
            node_type_t assignT = {
                .type = NODE_EXPR,
                .kind = ASSIGN_EXPR,
            };
            ret = createNode(assignT, eqTok, NULL);
            nodeAddChild(ret, lval);
            nodeAddChild(ret, rval);
            TRACE_END("assignExpr");
        } else if (nodeKind(lval) == PROP_ACCESS_EXPR) {
            TRACE_START("propAccessExpr");
            node_type_t propsetT = {
                .type = NODE_EXPR,
                .kind = PROP_SET_EXPR,
            };
            ret = createNode(propsetT, lval->tok, NULL);
            nodeAddChild(ret, vec_first(lval->children));
            nodeAddChild(ret, rval);
            TRACE_END("propAccessExpr");
        } else if (nodeKind(lval) == INDEX_GET_EXPR) {
            TRACE_START("indexGetExpr");
            node_type_t indexsetT = {
                .type = NODE_EXPR,
                .kind = INDEX_SET_EXPR,
            };
            ret = createNode(indexsetT, lval->tok, NULL);
            nodeAddChild(ret, vec_first(lval->children));
            nodeAddChild(ret, lval->children->data[1]);
            nodeAddChild(ret, rval);
            TRACE_END("indexGetExpr");
        } else if (nodeKind(lval) == SUPER_EXPR) {
            ASSERT(0); // FIXME
            TRACE_START("propAccessExpr (super.)");
            node_type_t propsetT = {
                .type = NODE_EXPR,
                .kind = PROP_SET_EXPR,
            };
            ret = createNode(propsetT, lval->tok, NULL);
            nodeAddChild(ret, vec_first(lval->children));
            nodeAddChild(ret, rval);
            TRACE_END("propAccessExpr (super.)");
            // TODO turn into propset
        } else {
            errorAtCurrent("invalid assignment lvalue");
        }
        TRACE_END("assignment");
        return ret;
    }
    TRACE_END("assignment");
    return lval;
}

static Node *logicOr() {
    TRACE_START("logicOr");
    Node *left = logicAnd();
    // precedence left to right ((this or that) or other)
    while (match(TOKEN_OR)) {
        TRACE_START("logicalExpr");
        Token orTok = current->previous;
        node_type_t orT = {
            .type = NODE_EXPR,
            .kind = LOGICAL_EXPR,
        };
        Node *orNode = createNode(orT, orTok, NULL);
        Node *right = logicAnd();
        nodeAddChild(orNode, left);
        nodeAddChild(orNode, right);
        left = orNode;
        TRACE_END("logicalExpr");
    }
    TRACE_END("logicOr");
    return left;
}

static Node *logicAnd() {
    TRACE_START("logicAnd");
    Node *left = equality();
    while (match(TOKEN_AND)) {
        TRACE_START("logicalExpr");
        Token andTok = current->previous;
        node_type_t andT = {
            .type = NODE_EXPR,
            .kind = LOGICAL_EXPR,
        };
        Node *andNode = createNode(andT, andTok, NULL);
        Node *right = equality();
        nodeAddChild(andNode, left);
        nodeAddChild(andNode, right);
        left = andNode;
        TRACE_END("logicalExpr");
    }
    TRACE_END("logicAnd");
    return left;
}

static Node *equality() {
    TRACE_START("equality");
    Node *left = comparison();
    while (match(TOKEN_EQUAL_EQUAL) || match(TOKEN_BANG_EQUAL)) {
        TRACE_START("binaryExpr");
        Token eqTok = current->previous;
        node_type_t eqT = {
            .type = NODE_EXPR,
            .kind = BINARY_EXPR,
        };
        Node *eqNode = createNode(eqT, eqTok, NULL);
        nodeAddChild(eqNode, left);
        Node *right = comparison();
        nodeAddChild(eqNode, right);
        left = eqNode;
        TRACE_END("binaryExpr");
    }
    TRACE_END("equality");
    return left;
}

static Node *comparison() {
    TRACE_START("comparison");
    Node *left = addition();
    while (match(TOKEN_LESS) || match(TOKEN_LESS_EQUAL) ||
           match(TOKEN_GREATER) || match(TOKEN_GREATER_EQUAL)) {
        TRACE_START("binaryExpr");
        Token cmpTok = current->previous;
        node_type_t cmpT = {
            .type = NODE_EXPR,
            .kind = BINARY_EXPR,
        };
        Node *cmpNode = createNode(cmpT, cmpTok, NULL);
        Node *right = addition();
        nodeAddChild(cmpNode, left);
        nodeAddChild(cmpNode, right);
        left = cmpNode;
        TRACE_END("binaryExpr");
    }
    TRACE_END("comparison");
    return left;
}

static Node *addition() {
    TRACE_START("addition");
    Node *left = byteManip();
    while (match(TOKEN_PLUS) || match(TOKEN_MINUS)) {
        TRACE_START("binaryExpr (+/-)");
        Token addTok = current->previous;
        node_type_t addT = {
            .type = NODE_EXPR,
            .kind = BINARY_EXPR,
        };
        Node *addNode = createNode(addT, addTok, NULL);
        Node *right = byteManip();
        nodeAddChild(addNode, left);
        nodeAddChild(addNode, right);
        left = addNode;
        TRACE_END("binaryExpr (+/-)");
    }
    TRACE_END("addition");
    return left;
}

static Node *byteManip() {
    TRACE_START("byteManip");
    Node *left = multiplication();
    while (match(TOKEN_XOR) || match(TOKEN_XAND)) {
        TRACE_START("binaryExpr (|,&)");
        Token byteTok = current->previous;
        node_type_t binT = {
            .type = NODE_EXPR,
            .kind = BINARY_EXPR,
        };
        Node *n = createNode(binT, byteTok, NULL);
        Node *right = multiplication();
        nodeAddChild(n, left);
        nodeAddChild(n, right);
        left = n;
        TRACE_END("binaryExpr (|,&)");
    }
    TRACE_END("byteManip");
    return left;
}

static Node *multiplication() {
    TRACE_START("multiplication");
    Node *left = unary();
    while (match(TOKEN_STAR) || match(TOKEN_SLASH)) {
        TRACE_START("binaryExpr");
        Token mulTok = current->previous;
        node_type_t mulT = {
            .type = NODE_EXPR,
            .kind = BINARY_EXPR,
        };
        Node *mulNode = createNode(mulT, mulTok, NULL);
        Node *right = unary();
        nodeAddChild(mulNode, left);
        nodeAddChild(mulNode, right);
        left = mulNode;
        TRACE_END("binaryExpr");
    }
    TRACE_END("multiplication");
    return left;
}

// precedence from right to left (!!a) == (!(!a))
static Node *unary() {
    TRACE_START("unary");
    if (match(TOKEN_BANG) || match(TOKEN_MINUS)) {
        Token unTok = current->previous;
        node_type_t unT = {
            .type = NODE_EXPR,
            .kind = UNARY_EXPR,
        };
        Node *unNode = createNode(unT, unTok, NULL);
        nodeAddChild(unNode, unary()); // slurp up from right
        TRACE_END("unary");
        return unNode;
    }
    Node *ret = call();
    TRACE_END("unary");
    return ret;
}

static Node *call() {
    TRACE_START("call");
    Node *expr = primary();
    bool oldInCallExpr = current->inCallExpr;
    Token lhsTok = current->previous;
    while (true) {
        if (match(TOKEN_LEFT_PAREN)) {
            current->inCallExpr = true;
            TRACE_START("callActual");
            node_type_t callT = {
                .type = NODE_EXPR,
                .kind = CALL_EXPR,
            };
            Node *callNode = createNode(callT, lhsTok, NULL);
            nodeAddChild(callNode, expr);
            expr = callNode;
            if (match(TOKEN_RIGHT_PAREN)) {
                // no args
            } else {
                bool inKwargs = false;
                while (true) {
                    if (check(TOKEN_IDENTIFIER) && peekTokN(1).type == TOKEN_COLON) { // kwargs
                        consume(TOKEN_IDENTIFIER, "Expected ident"); // ident
                        Token kwargTok = current->previous;
                        consume(TOKEN_COLON, "Expected colon"); // colon
                        Node *kwargVal = expression();
                        if (!inKwargs) inKwargs = true;
                        node_type_t kwargT = {
                            .type = NODE_STMT,
                            .kind = KWARG_IN_CALL_STMT
                        };
                        Node *kwargNode = createNode(kwargT, kwargTok, NULL);
                        nodeAddChild(kwargNode, kwargVal);
                        nodeAddChild(callNode, kwargNode);
                    } else {
                        if (inKwargs) {
                            errorAtCurrent("Cannot have a regular argument after a keyword argument");
                        }
                        Node *argExpr = expression();
                        nodeAddChild(callNode, argExpr);
                    }
                    if (!match(TOKEN_COMMA)) {
                        break;
                    }
                }
                consume(TOKEN_RIGHT_PAREN, "Expected ')' to end call expression");
            }
            TRACE_END("callActual");
        } else if (match(TOKEN_DOT)) {
            TRACE_START("propAccessExpr");
            consume(TOKEN_IDENTIFIER, "Expected identifier (property name) after '.' in property access");
            Token propName = current->previous;
            node_type_t propT = {
                .type = NODE_EXPR,
                .kind = PROP_ACCESS_EXPR,
            };
            Node *propAccess = createNode(propT, propName, NULL);
            nodeAddChild(propAccess, expr);
            expr = propAccess;
            TRACE_END("propAccessExpr");
        } else if (match(TOKEN_LEFT_BRACKET)) {
            TRACE_START("indexGetExpr");
            Token lBracket = current->previous;
            Node *indexExpr = expression();
            node_type_t idxGetT = {
                .type = NODE_EXPR,
                .kind = INDEX_GET_EXPR,
            };
            Node *idxGet = createNode(idxGetT, lBracket, NULL);
            nodeAddChild(idxGet, expr);
            nodeAddChild(idxGet, indexExpr);
            consume(TOKEN_RIGHT_BRACKET, "Expected ']' to end index expression");
            expr = idxGet;
            TRACE_END("indexGetExpr");
        } else {
            break;
        }
    }
    TRACE_END("call");
    current->inCallExpr = oldInCallExpr;
    return expr;
}

// FIXME: this creates right-associative binary operations, not left-associative
static Node *stringTogetherNodesBinop(vec_void_t *nodes, TokenType ttype, char *lexeme) {
    Node *ret = NULL;
    Node *last = NULL;
    int len = nodes->length;
    Node *n = NULL; int i = 0;
    vec_foreach(nodes, n, i) {
        if (i == len-1) {
            nodeAddChild(last, n);
        } else {
            node_type_t binop_T = {
                .type = NODE_EXPR,
                .kind = BINARY_EXPR,
            };
            Token tok;
            tok.line = getScanner()->line;
            tok.lexeme = lexeme;
            tok.start = lexeme;
            tok.length = strlen(lexeme);
            tok.type = ttype;
            Node *binop = createNode(binop_T, tok, NULL);
            nodeAddChild(binop, n);
            if (last) {
                nodeAddChild(last, binop);
            }
            if (i == 0) {
                ret = binop;
            }
            last = binop;
        }
    }
    return ret;
}

static Node *primary() {
    TRACE_START("primary");
    if (match(TOKEN_STRING_DQUOTE) || match(TOKEN_STRING_SQUOTE)) {
        char *interpBegin = NULL;
        TRACE_START("string");
        Token strTok = current->previous;
        char *str = strdup(tokStr(&strTok));
        str = str+1; // opening '"'
        str[strlen(str)-1] = '\0'; // closing '"'
        /*fprintf(stderr, "String: '%s'\n", str);*/
        char *beg = str;

        Scanner *oldScan = getScanner();
        Parser *oldParser = current;
        vec_void_t vnodes;
        vec_init(&vnodes);
        char *end = NULL;
        while ((interpBegin = strstr(beg, "${"))) {
            end = index(interpBegin, '}'); // FIXME: what if this is inside a single or double-quoted string?
            if (end == NULL) break;
            char *contents = calloc((end-interpBegin)+1, 1);
            ASSERT_MEM(contents);
            char *before = calloc((interpBegin-beg)+3, 1); // add room to surround with double-quotes
            ASSERT_MEM(before);
            strncpy(before+1, beg, (interpBegin-beg));
            before[0] = '"';
            before[strlen(before)] = '"';
            strncpy(contents, interpBegin+2, (end-interpBegin)-2);
            /*fprintf(stderr, "Interplation contents: '%s'\n", contents);*/
            /*fprintf(stderr, "Interplation before: '%s'\n", before);*/
            Scanner newScan;
            initScanner(&newScan, contents);
            setScanner(&newScan);
            Parser newParser;
            initParser(&newParser);
            int jmpres = 0;
            if ((jmpres = setjmp(newParser.errJmpBuf)) == JUMP_SET) {
                // do nothing
            } else if (jmpres == JUMP_PERFORMED) {
                outputParserErrors(&newParser, stderr);
                current = oldParser;
                error("Error in interpolation");
            } else {
                error("Error setting jump buffer for interpolation parser");
            }

            Node *inner = NULL;
            inner = parseExpression(&newParser);
            ASSERT(inner);
            ASSERT(!newParser.hadError);
            freeParser(&newParser);
            node_type_t nType = {
                .type = NODE_EXPR,
                .kind = LITERAL_EXPR,
                .litKind = STRING_TYPE,
            };
            Token litTok;
            litTok.start = before;
            litTok.length = strlen(before);
            litTok.line = strTok.line;
            litTok.type = TOKEN_STRING_DQUOTE;
            litTok.lexeme = before;
            Node *litNode = createNode(nType, litTok, NULL);
            vec_push(&vnodes, litNode);
            node_type_t callT = {
                .type = NODE_EXPR,
                .kind = CALL_EXPR,
            };
            Node *toStringCall = createNode(callT, syntheticToken("String"), NULL);
            node_type_t varT = {
                .type = NODE_EXPR,
                .kind = VARIABLE_EXPR,
            };
            Node *toStringVar = createNode(varT, syntheticToken("String"), NULL);
            nodeAddChild(toStringCall, toStringVar);
            nodeAddChild(toStringCall, inner);
            vec_push(&vnodes, toStringCall);
            beg = end+1;
        }
        if (vnodes.length > 0) {
            char *restStart = end+1;
            int restLen = (str+strlen(str))-end;
            char *rest = calloc(restLen+1+2, 1);
            ASSERT_MEM(rest);
            rest[0] = '"';
            strncpy(rest+1, restStart, restLen);
            rest[strlen(rest)] = '"';
            Token litTok;
            litTok.start = rest;
            litTok.length = strlen(rest);
            litTok.line = strTok.line;
            litTok.type = TOKEN_STRING_DQUOTE;
            litTok.lexeme = rest;
            /*fprintf(stderr, "Interpolation after: '%s'\n", rest);*/
            node_type_t nType = {
                .type = NODE_EXPR,
                .kind = LITERAL_EXPR,
                .litKind = STRING_TYPE,
            };
            Node *litNode = createNode(nType, litTok, NULL);
            vec_push(&vnodes, litNode);

        }

        setScanner(oldScan);
        current = oldParser;

        Node *ret = NULL;
        if (vnodes.length > 0) {
            ASSERT(vnodes.length > 1);
            ret = stringTogetherNodesBinop(&vnodes, TOKEN_PLUS, "+");
            /*fprintf(stderr, "Interpolation AST inner\n");*/
            /*char *output = outputASTString(ret, 0);*/
            /*fprintf(stderr, output);*/
            /*fprintf(stderr, "\n");*/
            vec_deinit(&vnodes);
        } else {
            node_type_t nType = {
                .type = NODE_EXPR,
                .kind = LITERAL_EXPR,
                .litKind = STRING_TYPE,
            };
            ret = createNode(nType, strTok, NULL);
        }
        TRACE_END("string");
        TRACE_END("primary");
        return ret;
    }
    if (match(TOKEN_STRING_STATIC)) {
        TRACE_START("string");
        Token strTok = current->previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = STATIC_STRING_TYPE,
        };
        Node *ret = createNode(nType, strTok, NULL);
        TRACE_END("string");
        TRACE_END("primary");
        return ret;
    }
    if (match(TOKEN_NUMBER)) {
        TRACE_START("number");
        Token numTok = current->previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = NUMBER_TYPE,
        };
        Node *ret = createNode(nType, numTok, NULL);
        TRACE_END("number");
        TRACE_END("primary");
        return ret;
    }
    if (match(TOKEN_NIL)) {
        TRACE_START("nil");
        Token nilTok = current->previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = NIL_TYPE,
        };
        Node *ret = createNode(nType, nilTok, NULL);
        TRACE_END("nil");
        TRACE_END("primary");
        return ret;
    }
    if (match(TOKEN_TRUE) || match(TOKEN_FALSE)) {
        TRACE_START("bool");
        Token boolTok = current->previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = BOOL_TYPE,
        };
        Node *ret = createNode(nType, boolTok, NULL);
        TRACE_END("bool");
        TRACE_END("primary");
        return ret;
    }
    if (match(TOKEN_IDENTIFIER)) {
        TRACE_START("varExpr");
        Token varName = current->previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = VARIABLE_EXPR,
        };
        Node *ret = createNode(nType, varName, NULL);
        TRACE_END("varExpr");
        TRACE_END("primary");
        return ret;
    }
    if (match(TOKEN_LEFT_BRACKET)) {
        TRACE_START("arrayExpr");
        Token lbrackTok = current->previous;
        node_type_t arrType = {
            .type = NODE_EXPR,
            .kind = ARRAY_EXPR,
        };
        Node *arr = createNode(arrType, lbrackTok, NULL);
        while (!match(TOKEN_RIGHT_BRACKET)) {
            if (match(TOKEN_COMMA)) {
                // continue
            } else {
                Node *el = expression();
                nodeAddChild(arr, el);
            }
        }
        TRACE_END("arrayExpr");
        TRACE_END("primary");
        return arr;
    }
    if (match(TOKEN_LEFT_PAREN)) {
        TRACE_END("groupExpr");
        Token lparenTok = current->previous;
        node_type_t gType = {
            .type = NODE_EXPR,
            .kind = GROUPING_EXPR,
        };
        Node *grouping = createNode(gType, lparenTok, NULL);
        Node *groupExpr = expression();
        nodeAddChild(grouping, groupExpr);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' to end group expression");
        TRACE_END("groupExpr");
        TRACE_END("primary");
        return grouping;
    }
    if (match(TOKEN_SUPER)) {
        TRACE_START("superExpr");
        Token superTok = current->previous;
        consume(TOKEN_DOT, "Expected '.' after keyword 'super'");
        consume(TOKEN_IDENTIFIER, "Expected identifier after 'super.'");
        Token identTok = current->previous;
        node_type_t sType = {
            .type = NODE_EXPR,
            .kind = SUPER_EXPR,
        };
        node_type_t pType = {
            .type = NODE_OTHER,
            .kind = TOKEN_NODE,
        };
        Node *superExpr = createNode(sType, superTok, NULL);
        Node *propNode = createNode(pType, identTok, NULL);
        nodeAddChild(superExpr, propNode);
        TRACE_END("superExpr");
        TRACE_END("primary");
        return superExpr;
    }
    if (match(TOKEN_THIS)) {
        TRACE_START("thisExpr");
        Token thisTok = current->previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = THIS_EXPR,
        };
        Node *thisExpr = createNode(nType, thisTok, NULL);
        TRACE_END("thisExpr");
        TRACE_END("primary");
        return thisExpr;
    }
    // anonymous function
    if (match(TOKEN_FUN)) {
        TRACE_START("anonFnDecl");
        Node *anonFn = funDeclaration(FUNCTION_TYPE_ANON);
        TRACE_END("anonFnDecl");
        TRACE_END("primary");
        return anonFn;
    }

    errorAtCurrent("Unexpected token");
    return NULL;
}
