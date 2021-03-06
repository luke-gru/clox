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

static int traceLvl = 0;
const char traceNesting[] = "  ";
static GetMoreSourceFn getMoreSourceFn = NULL;

static void printTraceNesting() {
    for (int i = 0; i < traceLvl; i++) {
        fprintf(stderr, traceNesting);
    }
}

static void _trace_start(const char *name) {
    if (CLOX_OPTION_T(traceParserCalls)) {
        printTraceNesting();
        fprintf(stderr, "[-- <%s> --]\n", name);
        traceLvl += 1;
    }
}
static void _trace_end(const char *name) {
    if (CLOX_OPTION_T(traceParserCalls)) {
        traceLvl -= 1;
        printTraceNesting();
        fprintf(stderr, "[-- </%s> --]\n", name);
    }
}

static Parser *current = NULL;

// initialize/reinitialize parser
void initParser(Parser *p) {
    p->hadError = false;
    p->panicMode = false;
    p->aborted = false;
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
    ASSERT(message);
    if (current->panicMode) { return; }
    current->panicMode = true;

    ObjString *str = hiddenString("", 0, NEWOBJ_FLAG_NONE);
    pushCStringFmt(str, "[Parse Error], (line %d) Error", token->line);

    if (token->type == TOKEN_EOF) {
        pushCStringFmt(str, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        pushCStringFmt(str, " at '%.*s'", token->length, tokStr(token));
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

static inline bool isCapital(char c) {
    return c >= 'A' && c <= 'Z';
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
    if (current->current.type != TOKEN_ERROR) {
        break;
    }

    fprintf(stderr, "Error\n");
    errorAtCurrent(tokStr(&current->current));
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
  if (current->aborted) return false;
  if (current->current.type == TOKEN_EOF && getMoreSourceFn) {
      /*fprintf(stderr, "getMoreSource called from match\n");*/
      getMoreSourceFn(&scanner, current);
      /*fprintf(stderr, "/getMoreSource called from match\n");*/
      if (current->aborted) {
          return false;
      }
      /*fprintf(stderr, "advance\n");*/
      advance();
      if (current->current.type == TOKEN_EOF) {
          current->aborted = true;
          return false;
      }
      /*fprintf(stderr, "/advance\n");*/
      if (!check(type)) return false;
      return true;
  }
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
static Node *bitManip(void);
static Node *multiplication(void);
static Node *unary(void);
static Node *call(void);
static Node *primary(void);
static Node *blockStmts(void);
static Node *classOrModuleBody(const char *name);
static Node *statement(void);
static Node *printStatement(void);
static Node *blockStatements(void);
static Node *expressionStatement(bool);


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

Node *parseMaybePartialStatement(Parser *p, GetMoreSourceFn fn) {
    getMoreSourceFn = fn;
    Parser *oldCurrent = current;
    current = p;
    advance(); // prime parser with parser.current
    TRACE_START("parseStatement");
    int jumpRes = setjmp(current->errJmpBuf);
    if (jumpRes == JUMP_PERFORMED) { // jumped, had error
        ASSERT(current->panicMode);
        current = (Parser*)oldCurrent;
        TRACE_END("parse (error)");
        return NULL;
    }
    Node *ret = declaration();
    TRACE_END("parseStatement");
    current = oldCurrent;
    getMoreSourceFn = NULL;
    return ret;
}

Node *parseClass(Parser *p) {
    Parser *oldCurrent = current;
    current = p;
    advance(); // prime parser with parser.current
    TRACE_START("parseClass");
    Node *ret = classOrModuleBody("classBody");
    TRACE_END("parseClass");
    current = oldCurrent;
    return ret;
}

static bool isAtEnd(void) {
    if (current->aborted) return true;
    bool isEnd = current->previous.type == TOKEN_EOF || check(TOKEN_EOF);
    if (!isEnd) return false;
    if (isEnd && getMoreSourceFn == NULL) {
        return true;
    } else if (isEnd && getMoreSourceFn) {
        /*fprintf(stderr, "getMoreSource called from isAtEnd\n");*/
        getMoreSourceFn(&scanner, current);
        /*fprintf(stderr, "/getMoreSource called from isAtEnd\n");*/
        if (current->aborted) {
            return true;
        }
        /*fprintf(stderr, "advance\n");*/
        advance();
        /*fprintf(stderr, "/advance\n");*/
        if (current->current.type == TOKEN_EOF) {
            return true;
        }
    }
    return false;
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
    volatile Parser *oldCurrent = current;
    current = p;
    node_type_t nType = {
        .type = NODE_STMT,
        .kind = STMTLIST_STMT,
    };
    Node *ret = createNode(nType, emptyTok(), NULL);
    int jumpRes = setjmp(current->errJmpBuf);
    if (jumpRes == JUMP_PERFORMED) { // jumped, had error
        ASSERT(current->panicMode);
        current = (Parser*)oldCurrent;
        TRACE_END("parse (error)");
        return NULL;
    }
    advance(); // prime parser with parser.current
    TRACE_START("parse");
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
    current = (Parser*)oldCurrent;
    return ret;
}

void nodeAddData(Node *node, void *data, NodeFreeDataCallback freeDataCb) {
    node->data = data;
    node->freeDataCb = freeDataCb;
}

static void freeSuperTokenCallback(Node *node) {
    ASSERT(node->data);
    FREE(Token, node->data);
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
        if (!isCapital(*tokStr(&nameTok))) {
            error("Class name must be a constant (start with a capital letter)");
        }
        node_type_t classType = {
            .type = NODE_STMT,
            .kind = CLASS_STMT,
        };
        Node *classNode = createNode(classType, nameTok, NULL);
        if (match(TOKEN_LESS)) {
            consume(TOKEN_IDENTIFIER, "Expected class name after '<' in class declaration");
            Token superName = current->previous;
            nodeAddData(classNode, (void*)copyToken(&superName), freeSuperTokenCallback);
        }

        consume(TOKEN_LEFT_BRACE, "Expected '{' after class name");
        Node *body = classOrModuleBody("classBody"); // block node
        consume(TOKEN_RIGHT_BRACE, "Expected '}' to end class body");
        nodeAddChild(classNode, body);
        TRACE_END("declaration");
        return classNode;
    }
    if (match(TOKEN_MODULE)) {
        consume(TOKEN_IDENTIFIER, "Expected module name (identifier) after keyword 'module'");
        Token nameTok = current->previous;
        if (!isCapital(*tokStr(&nameTok))) {
            error("Module name must be a constant (start with a capital letter)");
        }
        node_type_t modType = {
            .type = NODE_STMT,
            .kind = MODULE_STMT,
        };
        Node *modNode = createNode(modType, nameTok, NULL);
        consume(TOKEN_LEFT_BRACE, "Expected '{' after module name");
        Node *body = classOrModuleBody("moduleBody"); // block node
        consume(TOKEN_RIGHT_BRACE, "Expected '}' to end module body");
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
    /*if (match(TOKEN_LEFT_BRACE)) {*/
        /*Node *stmtList = blockStatements();*/
        /*node_type_t blockType = {*/
            /*.type = NODE_STMT,*/
            /*.kind = BLOCK_STMT,*/
        /*};*/
        /*Node *block = createNode(blockType, current->previous, NULL);*/
        /*nodeAddChild(block, stmtList);*/
        /*TRACE_END("statement");*/
        /*return block;*/
    /*}*/
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
            if (isCapital(*tokStr(&varTok))) {
                error("Can't set constants in a foreach loop");
            }
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
            Token elseTok = current->previous;
            if (check(TOKEN_IF)) { // else if
                Node *elseStmt = statement();
                nodeAddChild(ifNode, elseStmt);
            } else {
                consume(TOKEN_LEFT_BRACE, "Expected '{' after 'else'");
                Node *elseStmtList = blockStatements();
                Node *elseBlock = wrapStmtsInBlock(elseStmtList, elseTok);
                nodeAddChild(ifNode, elseBlock);
            }
        }
        TRACE_END("statement");
        return ifNode;
    }

    if (match(TOKEN_WHILE)) {
        Token whileTok = current->previous;
        consume(TOKEN_LEFT_PAREN, "Expected '(' after keyword 'while'");
        Node *cond = expression();
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'while' condition");
        consume(TOKEN_LEFT_BRACE, "Expected '{' after while");
        Token lbraceTok = current->previous;
        Node *blockStmtList = blockStatements();
        node_type_t whileT = {
            .type = NODE_STMT,
            .kind = WHILE_STMT,
        };
        Node *whileBlock = wrapStmtsInBlock(blockStmtList, lbraceTok);
        Node *whileNode = createNode(whileT, whileTok, NULL);
        nodeAddChild(whileNode, cond);
        nodeAddChild(whileNode, whileBlock);
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
            if (check(TOKEN_VAR)) {
                initializer = declaration();
            } else {
                initializer = expressionStatement(true);
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
            incrExpr = expressionStatement(false);
        }
        nodeAddChild(forNode, incrExpr);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'for' increment/decrement expression");
        consume(TOKEN_LEFT_BRACE, "Expected '{' after 'for'");
        Token lbraceTok = current->previous;
        Node *blockStmtList = blockStatements();
        Node *forBlock = wrapStmtsInBlock(blockStmtList, lbraceTok);
        nodeAddChild(forNode, forBlock);
        TRACE_END("statement");
        return forNode;
    }

    // try { } [catch ([Prefix::+]Error e) { }]+
    if (match(TOKEN_TRY)) {
        Token tryTok = current->previous;
        node_type_t nType = {
            .type = NODE_STMT,
            .kind = TRY_STMT,
        };
        TRACE_START("tryStatement");
        Node *_try = createNode(nType, tryTok, NULL);
        consume(TOKEN_LEFT_BRACE, "Expected '{' after keyword 'try'");
        Token lbraceTok = current->previous;
        Node *stmtList = blockStatements();
        Node *tryBlock = wrapStmtsInBlock(stmtList, lbraceTok);
        nodeAddChild(_try, tryBlock);
        int numCatches = 0;
        while (match(TOKEN_CATCH)) {
            numCatches++;
            Token catchTok = current->previous;
            consume(TOKEN_LEFT_PAREN, "Expected '(' after keyword 'catch'");
            Node *catchExpr = expression(); // should be constant expression (can be fully qualified)
            Token identToken;
            bool foundIdentToken = false;
            if (match(TOKEN_IDENTIFIER)) { // should be variable
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
            nodeAddChild(catchStmt, catchExpr);
            if (foundIdentToken) {
                node_type_t varT = {
                    .type = NODE_EXPR,
                    .kind = VARIABLE_EXPR,
                };
                Node *varExpr = createNode(varT, identToken, NULL);
                nodeAddChild(catchStmt, varExpr); // variable to be bound to in block
            }
            nodeAddChild(catchStmt, catchBlock);
            nodeAddChild(_try, catchStmt);
        }
        // try { ... } catch { ... } else { ... }
        if (match(TOKEN_ELSE)) {
            if (numCatches == 0) {
                errorAtCurrent("Try needs at least one catch statement with else");
            }
            Token elseTok = current->previous;
            consume(TOKEN_LEFT_BRACE, "Expected '{' after keyword 'else'");
            Token lbraceTok = current->previous;
            Node *elseStmtList = blockStatements();
            Node *elseBlock = wrapStmtsInBlock(elseStmtList, lbraceTok);
            node_type_t tryElseT = {
              .type = NODE_STMT,
              .kind = TRY_ELSE_STMT,
            };
            Node *elseStmt = createNode(tryElseT, elseTok, NULL);
            nodeAddChild(elseStmt, elseBlock);
            nodeAddChild(_try, elseStmt);
        }
        // try { ... } ensure { ... }
        if (match(TOKEN_ENSURE)) {
            Token ensureTok = current->previous;
            consume(TOKEN_LEFT_BRACE, "Expected '{' after keyword 'ensure'");
            Token lbraceTok = current->previous;
            Node *ensureStmtList = blockStatements();
            Node *ensureBlock = wrapStmtsInBlock(ensureStmtList, lbraceTok);
            node_type_t ensureT = {
              .type = NODE_STMT,
              .kind = ENSURE_STMT,
            };
            Node *ensureStmt = createNode(ensureT, ensureTok, NULL);
            nodeAddChild(ensureStmt, ensureBlock);
            nodeAddChild(_try, ensureStmt);
        }
        TRACE_END("tryStatement");
        TRACE_END("statement");
        return _try;
    }

    if (match(TOKEN_THROW)) {
        Token throwTok = current->previous;
        Node *expr = expression();
        consume(TOKEN_SEMICOLON, "Expected ';' to end 'throw' statement");
        node_type_t throwT = {
            .type = NODE_STMT,
            .kind = THROW_STMT,
        };
        Node *_throw = createNode(throwT, throwTok, NULL);
        nodeAddChild(_throw, expr);
        TRACE_END("statement");
        return _throw;
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
        consume(TOKEN_RIGHT_BRACE, "Expected '}' to end in body");
        nodeAddChild(inNode, body);
        TRACE_END("statement");
        return inNode;
    }
    Node *ret = expressionStatement(true);
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

static Node *expressionStatement(bool expectSemi) {
    TRACE_START("expressionStatement");
    Token tok = current->current;
    Node *expr = expression();
    node_type_t stmtT = {
        .type = NODE_STMT,
        .kind = EXPR_STMT,
    };
    Node *exprStmt = createNode(stmtT, tok, NULL);
    nodeAddChild(exprStmt, expr);
    if (expectSemi) {
        consume(TOKEN_SEMICOLON, "Expected ';' after expression");
    }
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
    if (isCapital(*tokStr(&identTok))) {
        error("Variable names cannot start with a capital letter. That's for constants.");
    }
    node_type_t nType = {
        .type = NODE_STMT,
        .kind = VAR_STMT,
    };
    Node *varDecl = createNode(nType, identTok, NULL);
    while (match(TOKEN_COMMA)) {
        consume(TOKEN_IDENTIFIER, "Expected identifier (variable name) "
                "after ',' in var declaration");
        Token tok = current->previous;
        Node *varNext = createNode(nType, tok, NULL);
        nodeAddChild(varDecl, varNext);
    }
    if (match(TOKEN_EQUAL)) {
        Node *expr = expression();
        nodeAddChild(varDecl, expr);
    } else {
        // uninitialized variable, set to nil later on
    }
    consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration");
    TRACE_END("varDeclaration");
    return varDecl;
}

static Node *expression(void) {
    TRACE_START("expression");
    Node *splatCall = NULL;
    Node *toBlockCall = NULL;
    if (current->inCallExpr && match(TOKEN_STAR)) {
        node_type_t splatType = {
            .type = NODE_EXPR,
            .kind = SPLAT_EXPR
        };
        splatCall = createNode(splatType, current->previous, NULL);
    }
    if (match(TOKEN_AMP)) {
        node_type_t toBlockT = {
            .type = NODE_EXPR,
            .kind = TO_BLOCK_EXPR
        };
        toBlockCall = createNode(toBlockT, current->previous, NULL);
    }
    Node *expr = assignment();
    if (splatCall != NULL) {
        nodeAddChild(splatCall, expr);
        expr = splatCall;
    } else if (toBlockCall != NULL) {
        nodeAddChild(toBlockCall, expr);
        expr = toBlockCall;
    }
    TRACE_END("expression");
    return expr;
}

static vec_nodep_t *createNodeVec(void) {
    vec_nodep_t *paramNodes = ALLOCATE(vec_nodep_t, 1);
    vec_init(paramNodes);
    return paramNodes;
}

static void freeParamNodesDataCb(Node *node) {
    ASSERT(node->data);
    vec_nodep_t *params = (vec_nodep_t*)node->data;
    Node *param; int pidx = 0;
    vec_foreach(params, param, pidx) {
        freeNode(param, true);
    }
    vec_deinit(params);
    FREE(vec_nodep_t, params);
}

// FUN keyword has already been parsed, for regular functions
static Node *funDeclaration(ParseFunctionType fnType) {
    TRACE_START("funDeclaration");
    Token nameTok = current->previous;
    if (fnType != FUNCTION_TYPE_ANON && fnType != FUNCTION_TYPE_BLOCK) {
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
        if (fnType == FUNCTION_TYPE_BLOCK && check(TOKEN_LEFT_BRACE)) { // no args for block
            // do nothing
        } else {
            consume(TOKEN_LEFT_PAREN, "Expect '(' after function name (identifier)");
        }
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
                if (!match(TOKEN_COMMA)) {
                    break;
                }
            } else if (match(TOKEN_AMP)) { // block param
                consume(TOKEN_IDENTIFIER, "Expect block parameter to have a name");
                Token paramTok = current->previous;
                node_type_t nType = {
                    .type = NODE_OTHER,
                    .kind = PARAM_NODE_BLOCK,
                };
                Node *n = createNode(nType, paramTok, NULL);
                vec_push(paramNodes, n);
                numParams++;
                lastParamKind = nType.kind;
                if (!check(TOKEN_RIGHT_PAREN)) {
                    error("Expected block parameter to be last parameter");
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
        if (fnType == FUNCTION_TYPE_BLOCK && numParams == 0 && check(TOKEN_LEFT_BRACE)) {
            // do nothing
        } else {
            consume(TOKEN_RIGHT_PAREN, "Expect ')' after function parameters");
        }
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
    } else if (fnType == FUNCTION_TYPE_ANON || fnType == FUNCTION_TYPE_BLOCK) {
        funcType = (node_type_t){
            .type = NODE_EXPR,
            .kind = ANON_FN_EXPR,
        };
    } else {
        ASSERT(0);
    }
    Node *funcNode = createNode(funcType, nameTok, NULL);
    nodeAddData(funcNode, (void*)paramNodes, freeParamNodesDataCb);
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

static bool checkAssignOp() {
    if (peekTokN(1).type == TOKEN_EQUAL) {
        return check(TOKEN_PLUS) ||
               check(TOKEN_MINUS) ||
               check(TOKEN_SLASH) ||
               check(TOKEN_STAR) ||
               check(TOKEN_PERCENT) ||
               check(TOKEN_SHOVEL_L) ||
               check(TOKEN_SHOVEL_R) ||
               check(TOKEN_PIPE) ||
               check(TOKEN_CARET) ||
               check(TOKEN_AMP);
    }
    return false;
}

static Node *assignment() {
    TRACE_START("assignment");
    Node *lval = logicOr();
    if (match(TOKEN_EQUAL)) { // regular assignment
        Token eqTok = current->previous;
        Node *rval = assignment(); // assignment goes right to left in precedence (a = (b = c))
        Node *ret = NULL;
        if (nodeKind(lval) == VARIABLE_EXPR || nodeKind(lval) == CONSTANT_EXPR) {
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
            freeNode(lval, false);
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
            freeNode(lval, false);
            TRACE_END("indexGetExpr");
        } else if (nodeKind(lval) == SUPER_EXPR) {
            ASSERT(0); // FIXME
            TRACE_START("propAccessExpr (super.)");
            node_type_t propsetT = {
                .type = NODE_EXPR,
                .kind = PROP_SET_EXPR,
            };
            ret = createNode(propsetT, lval->tok, NULL);
            nodeAddChild(ret, lval);
            nodeAddChild(ret, rval);
            TRACE_END("propAccessExpr (super.)");
            // TODO turn into propset
        } else {
            errorAtCurrent("invalid assignment lvalue");
        }
        TRACE_END("assignment");
        return ret;
    } else if (checkAssignOp()) { // ex: a += 1, a -= 1
        if (nodeKind(lval) == PROP_ACCESS_EXPR) {
            advance();
            Token opTok = current->previous;
            advance();
            Node *rval = assignment(); // assignment goes right to left in precedence (a *= (b += c))
            TRACE_START("propAccessExpr");
            node_type_t propsetT = {
                .type = NODE_EXPR,
                .kind = PROP_SET_BINOP_EXPR,
            };
            Node *ret = createNode(propsetT, opTok, NULL);
            nodeAddChild(ret, lval);
            nodeAddChild(ret, rval);
            TRACE_END("propAccessExpr");
            return ret;
        }
        if (nodeKind(lval) != VARIABLE_EXPR) {
            errorAtCurrent("invalid assignment lvalue");
        }
        TRACE_START("assignExpr (binAssignOp)");
        advance(); // past binary op
        Token opTok = current->previous;
        advance(); // past equal
        Node *rval = assignment(); // assignment goes right to left in precedence (a *= (b += c))
        node_type_t opT = {
            .type = NODE_EXPR,
            .kind = BINARY_ASSIGN_EXPR,
        };
        Node *ret = createNode(opT, opTok, NULL);
        nodeAddChild(ret, lval);
        nodeAddChild(ret, rval);
        TRACE_END("assignExpr (binAssignOp)");
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
    Node *left = bitManip();
    while ((check(TOKEN_PLUS) || check(TOKEN_MINUS)) && peekTokN(1).type != TOKEN_EQUAL) {
        advance();
        TRACE_START("binaryExpr (+/-)");
        Token addTok = current->previous;
        node_type_t addT = {
            .type = NODE_EXPR,
            .kind = BINARY_EXPR,
        };
        Node *addNode = createNode(addT, addTok, NULL);
        Node *right = bitManip();
        nodeAddChild(addNode, left);
        nodeAddChild(addNode, right);
        left = addNode;
        TRACE_END("binaryExpr (+/-)");
    }
    TRACE_END("addition");
    return left;
}

static Node *bitManip() {
    TRACE_START("bitManip");
    Node *left = multiplication();
    while ((check(TOKEN_PIPE) || check(TOKEN_AMP) || check(TOKEN_CARET) ||
            check(TOKEN_SHOVEL_L) || check(TOKEN_SHOVEL_R)) &&
            peekTokN(1).type != TOKEN_EQUAL) {
        advance();
        TRACE_START("binaryExpr (|,&,^,<<,>>)");
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
        TRACE_END("binaryExpr (|,&,^,<<,>>)");
    }
    TRACE_END("bitManip");
    return left;
}

static Node *multiplication() {
    TRACE_START("multiplication");
    Node *left = unary();
    while ((check(TOKEN_STAR) || check(TOKEN_SLASH) || check(TOKEN_PERCENT)) &&
            peekTokN(1).type != TOKEN_EQUAL) {
        advance();
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

static Node *blockDecl() {
    return funDeclaration(FUNCTION_TYPE_BLOCK);
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
            bool seenBlockArg = false;
            (void)seenBlockArg;
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
                            if (check(TOKEN_AMP)) { // block arg
                                seenBlockArg = true;
                            } else {
                                errorAtCurrent("Cannot have a regular argument after a keyword argument");
                            }
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
            // block
            if (match(TOKEN_ARROW)) {
                TRACE_START("callBlock");
                node_type_t blockCallT = {
                    .type = NODE_EXPR,
                    .kind = CALL_BLOCK_EXPR,
                };
                Node *fnCall = blockDecl(); // TODO
                Node *blockFn = createNode(blockCallT, current->previous, NULL);
                nodeAddChild(blockFn, expr);
                nodeAddChild(blockFn, fnCall);
                expr = blockFn;
                TRACE_END("callBlock");
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
            tok.startIdx = 0;
            tok.length = strlen(lexeme);
            tok.type = ttype;
            tok.alloced = false;
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
            char *contents = calloc(1, (end-interpBegin)+1);
            ASSERT_MEM(contents);
            char *before = calloc(1, (interpBegin-beg)+1); // add room to surround with double-quotes
            ASSERT_MEM(before);
            strncpy(before, beg, (interpBegin-beg));
            strncpy(contents, interpBegin+2, (end-interpBegin)-2);
            /*fprintf(stderr, "Interplation contents: '%s'\n", contents);*/
            /*fprintf(stderr, "Interplation before: '%s'\n", before);*/
            Scanner *newScan = ALLOCATE(Scanner, 1);
            initScanner(newScan, contents);
            setScanner(newScan);
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
            litTok.startIdx = 0;
            litTok.length = strlen(before);
            litTok.line = strTok.line;
            litTok.type = TOKEN_STRING_DQUOTE;
            litTok.lexeme = before;
            litTok.alloced = true;
            Node *litNode = createNode(nType, litTok, NULL);
            vec_push(&vnodes, litNode);
            node_type_t callT = {
                .type = NODE_EXPR,
                .kind = CALL_EXPR,
            };
            // stringify the expression by calling String() on the expr result
            Node *toStringCall = createNode(callT, syntheticToken("String"), NULL);
            node_type_t varT = {
                .type = NODE_EXPR,
                .kind = CONSTANT_EXPR,
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
            char *rest = calloc(1, restLen+1);
            ASSERT_MEM(rest);
            strncpy(rest, restStart, restLen);
            Token litTok;
            litTok.startIdx = 0;
            litTok.length = strlen(rest);
            litTok.line = strTok.line;
            litTok.type = TOKEN_STRING_DQUOTE;
            litTok.lexeme = rest;
            litTok.alloced = true;
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
        node_type_t nType;
        if (isCapital(*tokStr(&varName))) {
            nType = (node_type_t){
                .type = NODE_EXPR,
                .kind = CONSTANT_EXPR,
            };
        } else {
            nType = (node_type_t){
                .type = NODE_EXPR,
                .kind = VARIABLE_EXPR,
            };
        }
        Node *ret = createNode(nType, varName, NULL);
        while (match(TOKEN_DICE)) {
            consume(TOKEN_IDENTIFIER, "Expected identifier after '::'");
            node_type_t constLookupType = {
                .type = NODE_EXPR,
                .kind = CONSTANT_LOOKUP_EXPR
            };
            Node *constLookupNode = createNode(constLookupType, current->previous, NULL);
            nodeAddChild(constLookupNode, ret);
            ret = constLookupNode;
        }
        TRACE_END("varExpr");
        TRACE_END("primary");
        return ret;
    }
    if (match(TOKEN_DICE)) {
        consume(TOKEN_IDENTIFIER, "Expected identifier after '::'");
        node_type_t constLookupType = {
            .type = NODE_EXPR,
            .kind = CONSTANT_LOOKUP_EXPR
        };
        Node *constLookupNode = createNode(constLookupType, current->previous, NULL);
        while (match(TOKEN_DICE)) {
            consume(TOKEN_IDENTIFIER, "Expected identifier after '::'");
            Node *constLookupNodeInner = createNode(constLookupType, current->previous, NULL);
            nodeAddChild(constLookupNodeInner, constLookupNode);
            constLookupNode = constLookupNodeInner;
        }
        return constLookupNode;
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
    // %{key: val}
    if (check(TOKEN_PERCENT) && peekTokN(1).type == TOKEN_LEFT_BRACE) {
        TRACE_START("mapExpr");
        advance(); advance();
        Token lbrackTok = current->previous;
        node_type_t mapType = {
            .type = NODE_EXPR,
            .kind = MAP_EXPR,
        };
        Node *map = createNode(mapType, lbrackTok, NULL);
        while (true) {
            if (check(TOKEN_RIGHT_BRACE)) break;
            Node *key = expression();
            nodeAddChild(map, key);
            consume(TOKEN_COLON, "Expected colon after key in map literal");
            Node *val = expression();
            nodeAddChild(map, val);
            if (match(TOKEN_COMMA)) {
                // continue
            } else {
                break;
            }
        }
        consume(TOKEN_RIGHT_BRACE, "Expected '}' to end map literal");
        TRACE_END("mapExpr");
        TRACE_END("primary");
        return map;
    }
    // %"regex"
    if (check(TOKEN_PERCENT) && (peekTokN(1).type == TOKEN_STRING_DQUOTE || peekTokN(1).type == TOKEN_STRING_SQUOTE)) {
        advance(); advance();
        TRACE_START("regexExpr");
        Token strTok = current->previous;
        node_type_t reType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = REGEX_TYPE,
        };
        Node *regex = createNode(reType, strTok, NULL);
        TRACE_END("regexExpr");
        TRACE_END("primary");
        return regex;
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
        if (match(TOKEN_DICE)) {
            consume(TOKEN_IDENTIFIER, "Expected identifier after '::'");
            node_type_t constLookupType = {
                .type = NODE_EXPR,
                .kind = CONSTANT_LOOKUP_EXPR
            };
            Node *constLookupNode = createNode(constLookupType, current->previous, NULL);
            nodeAddChild(constLookupNode, thisExpr);
            thisExpr = constLookupNode;
        }
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
