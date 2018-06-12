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
static Node *blockStmts(void);
static Node *classBody(void);

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
    while (!isAtEnd()) {
        Node *stmt = declaration();
        ASSERT(stmt->type.type == NODE_STMT);
        nodeAddChild(ret, stmt);
    }
    return ret;
}

static Node *declaration(void) {
    if (match(TOKEN_VAR)) {
        return varDeclaration();
    }
    if (check(TOKEN_FUN) && peekTokN(1).type == TOKEN_IDENTIFIER) {
        advance();
        return funDeclaration();
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
        return classNode;
    }
    fprintf(stderr, "declaration fallthru\n");
    return NULL;
}

// '{' already parsed. Continue parsing up to and including closing '}'
static Node *classBody() {
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
    return block;
}

static Node *varDeclaration(void) {
    consume(TOKEN_IDENTIFIER, "Expected identifier after keyword 'var'");
    Token identTok = parser.previous;
    consume(TOKEN_EQUAL, "Expected '=' after identifier in variable declaration");
    Node *expr = expression();
    ASSERT(expr != NULL)
    consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration");
    node_type_t nType = {
        .type = NODE_STMT,
        .kind = VAR_STMT,
    };
    Node *varDecl = createNode(nType, identTok, NULL);
    nodeAddChild(varDecl, expr);
    return varDecl;
}

static Node *expression(void) {
    if (match(TOKEN_STRING)) {
        Token strTok = parser.previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = STRING_TYPE,
        };
        return createNode(nType, strTok, NULL);
    }
    if (match(TOKEN_NUMBER)) {
        Token numTok = parser.previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = NUMBER_TYPE,
        };
        return createNode(nType, numTok, NULL);
    }
    if (match(TOKEN_NIL)) {
        Token nilTok = parser.previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = NIL_TYPE,
        };
        return createNode(nType, nilTok, NULL);
    }
    if (match(TOKEN_TRUE) || match(TOKEN_FALSE)) {
        Token boolTok = parser.previous;
        node_type_t nType = {
            .type = NODE_EXPR,
            .kind = LITERAL_EXPR,
            .litKind = BOOL_TYPE,
        };
        return createNode(nType, boolTok, NULL);
    }
    fprintf(stderr, "NULL expression\n");
    return NULL;
}

static vec_nodep_t *createNodeVec(void) {
    vec_nodep_t *paramNodes = ALLOCATE(vec_nodep_t, 1);
    ASSERT_MEM(paramNodes);
    vec_init(paramNodes);
    return paramNodes;
}

static Node *funDeclaration(void) {
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
    return funcNode;
}

// a list of stmts
static Node *blockStmts() {
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
    return blockNode;
}

/*static bool identifiersEqual(Token* a, Token* b) {*/
  /*if (a->length != b->length) return false;*/
  /*return memcmp(a->start, b->start, a->length) == 0;*/
/*}*/
