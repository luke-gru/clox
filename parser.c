#include <string.h>
#include <stdio.h>
#include "parser.h"
#include "options.h"
#include "memory.h"

#ifndef ASSERT
#define ASSERT(n)
#endif
#ifndef ASSERT_MEM
#define ASSERT_MEM(n)
#endif

#define TRACE_START(name) _trace_start(name)
#define TRACE_END(name) _trace_end(name)

static void _trace_start(const char *name) {
    /*fprintf(stderr, "[-- <%s> --]\n", name);*/
}
static void _trace_end(const char *name) {
    /*fprintf(stderr, "[-- </%s> --]\n", name);*/
}

// global
Parser parser;

// initialize/reinitialize parser
void initParser(void) {
    parser.hadError = false;
    parser.panicMode = false;
    memset(&parser.current, 0, sizeof(Token));
    memset(&parser.previous, 0, sizeof(Token));
    if (parser.peekBuf.length > 0) {
        vec_deinit(&parser.peekBuf);
    } else {
        vec_init(&parser.peekBuf);
    }
}

static void errorAt(Token *token, const char *message) {
  if (parser.panicMode) return;
  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing.
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char *message) {
  errorAt(&parser.previous, message);
}
static void errorAtCurrent(const char *message) {
  errorAt(&parser.current, message);
}

// takes into account peekbuffer
static Token nextToken() {
    if (parser.peekBuf.length > 0) {
        Token val = vec_first(&parser.peekBuf);
        vec_splice(&parser.peekBuf, 0, 1);
        return val;
    } else {
        return scanToken();
    }
}

// Fill `parser.current` with next token in stream
static void advance() {
  parser.previous = parser.current;

  // parse the next non-error token
  for (;;) {
    parser.current = nextToken();
    if (parser.current.type != TOKEN_ERROR) break;

    errorAtCurrent(parser.current.start);
  }
}

// Expect current token to be of given type, otherwise error with given
// message.
static void consume(TokenType type, const char *message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

// Check that the current token is of given type
static bool check(TokenType type) {
  return parser.current.type == type;
}

static Token peekTokN(int n) {
    ASSERT(n > 0);
    if (parser.peekBuf.length < n) {
        for (int i = parser.peekBuf.length; i < n; i++) {
            Token tok = scanToken();
            vec_push(&parser.peekBuf, tok);
            if (tok.type == TOKEN_EOF) break;
        }
        return vec_last(&parser.peekBuf);
    } else {
        return parser.peekBuf.data[n-1];
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
static Node *funDeclaration(void);
static Node *expression(void);
static Node *assignment(void);
static Node *logicOr(void);
static Node *logicAnd(void);
static Node *equality(void);
static Node *comparison(void);
static Node *addition(void);
static Node *multiplication(void);
static Node *unary(void);
static Node *call(void);
static Node *primary(void);
static Node *blockStmts(void);
static Node *classBody(void);
static Node *statement(void);
static Node *printStatement(void);
static Node *blockStatements(void);
static Node *expressionStatement(void);

static bool isAtEnd(void) {
    return parser.previous.type == TOKEN_EOF || check(TOKEN_EOF);
}

// returns program node that contains list of statement nodes
Node *parse(void) {
    initParser();
    node_type_t nType = {
        .type = NODE_STMT,
        .kind = STMTLIST_STMT,
    };
    Node *ret = createNode(nType, emptyTok(), NULL);
    advance(); // prime parser with parser.current
    TRACE_START("parse");
    while (!isAtEnd()) {
        Node *stmt = declaration();
        ASSERT(stmt->type.type == NODE_STMT);
        nodeAddChild(ret, stmt);
    }
    TRACE_END("parse");
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
        Node *ret = funDeclaration();
        TRACE_END("declaration");
        return ret;
    }
    if (match(TOKEN_CLASS)) {
        consume(TOKEN_IDENTIFIER, "Expected class name (identifier) after keyword 'class'");
        Token nameTok = parser.previous;
        node_type_t classType = {
            .type = NODE_STMT,
            .kind = CLASS_STMT,
        };
        Node *classNode = createNode(classType, nameTok, NULL);
        if (match(TOKEN_LESS)) {
            consume(TOKEN_IDENTIFIER, "Expected class name after '<' in class declaration");
            Token superName = parser.previous;
            nodeAddData(classNode, (void*)copyToken(&superName));
        }

        consume(TOKEN_LEFT_BRACE, "Expected '{' after class name");
        Node *body = classBody(); // block node
        nodeAddChild(classNode, body);
        TRACE_END("declaration");
        return classNode;
    }
    if (match(TOKEN_MODULE)) {
        // TODO
        TRACE_END("declaration");
        return NULL;
    }
    Node *ret = statement();
    TRACE_END("declaration");
    return ret;
}

static Node *wrapStmtsInBlock(Node *stmtList, Token lbraceTok) {
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
        Node *block = createNode(blockType, parser.previous, NULL);
        nodeAddChild(block, stmtList);
        TRACE_END("statement");
        return block;
    }
    if (match(TOKEN_IF)) {
        Token ifTok = parser.previous;
        consume(TOKEN_LEFT_PAREN, "Expected '(' after keyword 'if'");
        node_type_t ifType = {
            .type = NODE_STMT,
            .kind = IF_STMT,
        };
        Node *ifNode = createNode(ifType, ifTok, NULL);
        Node *cond = expression();
        consume(TOKEN_RIGHT_PAREN, "Expected ')' to end 'if' condition");
        consume(TOKEN_LEFT_BRACE, "Expected '{' after 'if' condition");
        Token lbraceTok = parser.previous;
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
        Token whileTok = parser.previous;
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
        Token forTok = parser.previous;
        Node *forNode = createNode(forT, forTok, NULL);
        consume(TOKEN_LEFT_PAREN, "Expected '(' after keyword 'for'");
        Node *initializer = NULL; // can be null
        if (match(TOKEN_SEMICOLON)) {
            // leave NULL
        } else {
            if (match(TOKEN_VAR)) {
                initializer = varDeclaration();
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

    // try { } catch (Error e) { }
    if (match(TOKEN_TRY)) {
        Token tryTok = parser.previous;
        node_type_t nType = {
            .type = NODE_STMT,
            .kind = TRY_STMT,
        };
        Node *try = createNode(nType, tryTok, NULL);
        consume(TOKEN_LEFT_BRACE, "Expected '{' after keyword 'try'");
        Token lbraceTok = parser.previous;
        Node *stmtList = blockStatements();
        Node *tryBlock = wrapStmtsInBlock(stmtList, lbraceTok);
        nodeAddChild(try, tryBlock);
        while (match(TOKEN_CATCH)) {
            Token catchTok = parser.previous;
            consume(TOKEN_LEFT_PAREN, "Expected '(' after keyword 'catch'");
            Node *catchExpr = expression();
            Token *identToken = NULL;
            if (match(TOKEN_IDENTIFIER)) {
                identToken = &parser.previous;
            }
            consume(TOKEN_RIGHT_PAREN, "Expected ')' to end 'catch' expression");
            consume(TOKEN_LEFT_BRACE, "Expected '{' after 'catch' expression");
            lbraceTok = parser.previous;
            Node *catchStmtList = blockStatements();
            Node *catchBlock = wrapStmtsInBlock(catchStmtList, lbraceTok);
            node_type_t catchT = {
                .type = NODE_STMT,
                .kind = CATCH_STMT,
            };
            Node *catchStmt = createNode(catchT, catchTok, NULL);
            nodeAddChild(catchStmt, catchExpr); // class or string or instance, etc.
            if (identToken != NULL) {
                node_type_t varT = {
                    .type = NODE_EXPR,
                    .kind = VARIABLE_EXPR,
                };
                Node *varExpr = createNode(varT, *identToken, NULL);
                nodeAddChild(catchStmt, varExpr); // variable to be bound to in block
            }
            nodeAddChild(catchStmt, catchBlock);
            nodeAddChild(try, catchStmt);
        }
        TRACE_END("statement");
        return try;
    }

    if (match(TOKEN_THROW)) {
        Token throwTok = parser.previous;
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
        Token contTok = parser.previous;
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
        Token breakTok = parser.previous;
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
        Token retTok = parser.previous;
        node_type_t retT = {
            .type = NODE_STMT,
            .kind = RETURN_STMT,
        };
        Node *retNode = createNode(retT, retTok, NULL);
        if (match(TOKEN_SEMICOLON)) {
        } else {
            Node *retExpr = expression();
            nodeAddChild(retNode, retExpr);
            consume(TOKEN_SEMICOLON, "Expected ';' to end 'return' statement");
        }
        TRACE_END("statement");
        return retNode;
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
    Node *printNode = createNode(printType, parser.previous, NULL);
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
    Node *stmtList = createNode(stmtListT, parser.previous, NULL);
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
    Token tok = parser.current;
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
static Node *classBody() {
    TRACE_START("classBody");
    Token lbraceTok = parser.previous;
    node_type_t nType = {
        .type = NODE_STMT,
        .kind = STMTLIST_STMT,
    };
    Node *stmtListNode = createNode(nType, lbraceTok, NULL);
    while (!isAtEnd() && !check(TOKEN_RIGHT_BRACE)) {
        Node *decl = declaration();
        nodeAddChild(stmtListNode, decl);
    }
    consume(TOKEN_RIGHT_BRACE, "Expected '}' to end class body");
    node_type_t blockType = {
        .type = NODE_STMT,
        .kind = BLOCK_STMT,
    };
    Node *block = createNode(blockType, lbraceTok, NULL);
    nodeAddChild(block, stmtListNode);
    TRACE_END("classBody");
    return block;
}

// TOKEN_VAR is already consumed.
static Node *varDeclaration(void) {
    TRACE_START("varDeclaration");
    consume(TOKEN_IDENTIFIER, "Expected identifier after keyword 'var'");
    Token identTok = parser.previous;
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
    Node *ret = assignment();
    TRACE_END("expression");
    return ret;
}

static vec_nodep_t *createNodeVec(void) {
    vec_nodep_t *paramNodes = ALLOCATE(vec_nodep_t, 1);
    ASSERT_MEM(paramNodes);
    vec_init(paramNodes);
    return paramNodes;
}

static Node *funDeclaration(void) {
    TRACE_START("funDeclaration");
    consume(TOKEN_IDENTIFIER, "Expect function name (identifier) after 'fun' keyword");
    Token nameTok = parser.previous;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name (identifier)");
    vec_nodep_t *paramNodes = createNodeVec();
    while (match(TOKEN_IDENTIFIER)) {
        Token paramTok = parser.previous;
        node_type_t nType = {
            .type = NODE_OTHER,
            .kind = PARAM_NODE,
        };
        Node *n = createNode(nType, paramTok, NULL);
        vec_push(paramNodes, n);
        if (!match(TOKEN_COMMA)) {
            break;
        }
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after function parameters");
    consume(TOKEN_LEFT_BRACE, "Expect '{' after function parameter list");
    Token lbrace = parser.previous;
    Node *stmtList = blockStmts();
    node_type_t funcType = {
        .type = NODE_STMT,
        .kind = FUNCTION_STMT,
    };
    node_type_t blockType = {
        .type = NODE_STMT,
        .kind = BLOCK_STMT,
    };
    Node *blockNode = createNode(blockType, lbrace, NULL);
    nodeAddChild(blockNode, stmtList);
    Node *funcNode = createNode(funcType, nameTok, NULL);
    nodeAddData(funcNode, (void*)paramNodes);
    nodeAddChild(funcNode, blockNode);
    TRACE_END("funDeclaration");
    return funcNode;
}

// a list of stmts
static Node *blockStmts() {
    TRACE_START("blockStmts");
    Token lbraceTok = parser.previous;
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
        Token eqTok = parser.previous;
        Node *rval = assignment(); // assignment goes right to left in precedence (a = (b = c))
        node_type_t assignT = {
            .type = NODE_EXPR,
            .kind = ASSIGN_EXPR,
        };
        // TODO: match logic in java version
        Node *assign = createNode(assignT, eqTok, NULL);
        nodeAddChild(assign, lval);
        nodeAddChild(assign, rval);
        TRACE_END("assignment");
        return assign;
    }
    TRACE_END("assignment");
    return lval;
}

static Node *logicOr() {
    TRACE_START("logicOr");
    Node *left = logicAnd();
    // precedence left to right ((this or that) or other)
    while (match(TOKEN_OR)) {
        Token orTok = parser.previous;
        node_type_t orT = {
            .type = NODE_EXPR,
            .kind = LOGICAL_EXPR,
        };
        Node *orNode = createNode(orT, orTok, NULL);
        Node *right = logicAnd();
        nodeAddChild(orNode, left);
        nodeAddChild(orNode, right);
        left = orNode;
    }
    TRACE_END("logicOr");
    return left;
}

static Node *logicAnd() {
    TRACE_START("logicAnd");
    Node *left = equality();
    while (match(TOKEN_AND)) {
        Token andTok = parser.previous;
        node_type_t andT = {
            .type = NODE_EXPR,
            .kind = LOGICAL_EXPR,
        };
        Node *andNode = createNode(andT, andTok, NULL);
        Node *right = equality();
        nodeAddChild(andNode, left);
        nodeAddChild(andNode, right);
        left = andNode;
    }
    TRACE_END("logicAnd");
    return left;
}

static Node *equality() {
    TRACE_START("equality");
    Node *left = comparison();
    while (match(TOKEN_EQUAL_EQUAL) || match(TOKEN_BANG_EQUAL)) {
        Token eqTok = parser.previous;
        node_type_t eqT = {
            .type = NODE_EXPR,
            .kind = BINARY_EXPR,
        };
        Node *eqNode = createNode(eqT, eqTok, NULL);
        nodeAddChild(eqNode, left);
        Node *right = comparison();
        nodeAddChild(eqNode, right);
        left = eqNode;
    }
    TRACE_END("equality");
    return left;
}

static Node *comparison() {
    TRACE_START("comparison");
    Node *left = addition();
    while (match(TOKEN_LESS) || match(TOKEN_LESS_EQUAL) ||
           match(TOKEN_GREATER) || match(TOKEN_GREATER_EQUAL)) {
        Token cmpTok = parser.previous;
        node_type_t cmpT = {
            .type = NODE_EXPR,
            .kind = BINARY_EXPR,
        };
        Node *cmpNode = createNode(cmpT, cmpTok, NULL);
        Node *right = addition();
        nodeAddChild(cmpNode, left);
        nodeAddChild(cmpNode, right);
        left = cmpNode;
    }
    TRACE_END("comparison");
    return left;
}

static Node *addition() {
    TRACE_START("addition");
    Node *left = multiplication();
    while (match(TOKEN_PLUS) || match(TOKEN_MINUS)) {
        Token addTok = parser.previous;
        node_type_t addT = {
            .type = NODE_EXPR,
            .kind = BINARY_EXPR,
        };
        Node *addNode = createNode(addT, addTok, NULL);
        Node *right = multiplication();
        nodeAddChild(addNode, left);
        nodeAddChild(addNode, right);
        left = addNode;
    }
    TRACE_END("addition");
    return left;
}

static Node *multiplication() {
    TRACE_START("multiplication");
    Node *left = unary();
    while (match(TOKEN_STAR) || match(TOKEN_SLASH)) {
        Token mulTok = parser.previous;
        node_type_t mulT = {
            .type = NODE_EXPR,
            .kind = BINARY_EXPR,
        };
        Node *mulNode = createNode(mulT, mulTok, NULL);
        Node *right = unary();
        nodeAddChild(mulNode, left);
        nodeAddChild(mulNode, right);
        left = mulNode;
    }
    TRACE_END("multiplication");
    return left;
}

// precedence from right to left (!!a) == (!(!a))
static Node *unary() {
    TRACE_START("unary");
    if (match(TOKEN_BANG) || match(TOKEN_MINUS)) {
        Token unTok = parser.previous;
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
    while (true) {
        if (match(TOKEN_LEFT_PAREN)) {
            node_type_t callT = {
                .type = NODE_EXPR,
                .kind = CALL_EXPR,
            };
            Token lparenTok = parser.previous;
            Node *callNode = createNode(callT, lparenTok, NULL);
            nodeAddChild(callNode, expr);
            expr = callNode;
            if (match(TOKEN_RIGHT_PAREN)) {
                // no args
            } else {
                while (true) {
                    Node *argExpr = expression();
                    nodeAddChild(callNode, argExpr);
                    if (!match(TOKEN_COMMA)) {
                        break;
                    }
                }
                consume(TOKEN_RIGHT_PAREN, "Expected ')' to end call expression");
            }
        } else if (match(TOKEN_DOT)) {
            consume(TOKEN_IDENTIFIER, "Expected identifier (property name) after '.' in property access");
            Token propName = parser.previous;
            node_type_t propT = {
                .type = NODE_EXPR,
                .kind = PROP_ACCESS_EXPR,
            };
            Node *propAccess = createNode(propT, propName, NULL);
            nodeAddChild(propAccess, expr);
            expr = propAccess;
        } else if (match(TOKEN_LEFT_BRACKET)) {
            Token lBracket = parser.previous;
            Node *indexExpr = expression();
            node_type_t idxGetT = {
                .type = NODE_EXPR,
                .kind = INDEX_GET_EXPR,
            };
            Node *idxGet = createNode(idxGetT, lBracket, NULL);
            nodeAddChild(idxGet, expr);
            nodeAddChild(idxGet, indexExpr);
            expr = idxGet;
        } else {
            break;
        }
    }
    TRACE_END("call");
    return expr;
}

static Node *primary() {
    TRACE_START("primary");
    if (match(TOKEN_STRING)) {
        Token strTok = parser.previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = STRING_TYPE,
        };
        Node *ret = createNode(nType, strTok, NULL);
        TRACE_END("primary");
        return ret;
    }
    if (match(TOKEN_NUMBER)) {
        Token numTok = parser.previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = NUMBER_TYPE,
        };
        Node *ret = createNode(nType, numTok, NULL);
        TRACE_END("primary");
        return ret;
    }
    if (match(TOKEN_NIL)) {
        Token nilTok = parser.previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = NIL_TYPE,
        };
        Node *ret = createNode(nType, nilTok, NULL);
        TRACE_END("primary");
        return ret;
    }
    if (match(TOKEN_TRUE) || match(TOKEN_FALSE)) {
        Token boolTok = parser.previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = BOOL_TYPE,
        };
        Node *ret = createNode(nType, boolTok, NULL);
        TRACE_END("primary");
        return ret;
    }
    if (match(TOKEN_IDENTIFIER)) {
        Token varName = parser.previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = VARIABLE_EXPR,
        };
        Node *ret = createNode(nType, varName, NULL);
        TRACE_END("primary");
        return ret;
    }
    if (match(TOKEN_LEFT_BRACKET)) {
        Token lbrackTok = parser.previous;
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
        TRACE_END("primary");
        return arr;
    }
    // TODO: arrays and anonymous functions `var f = fun() { }`
    fprintf(stderr, "primary fallthru: %s\n", tokTypeStr(parser.current.type));
}

/*static bool identifiersEqual(Token* a, Token* b) {*/
  /*if (a->length != b->length) return false;*/
  /*return memcmp(a->start, b->start, a->length) == 0;*/
/*}*/
