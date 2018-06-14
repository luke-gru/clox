#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "compiler.h"
#include "nodes.h"
#include "scanner.h"
#include "parser.h"
#include "value.h"
#include "debug.h"
#include "options.h"

typedef struct {
  // The name of the local variable.
  Token name;

  // The depth in the scope chain that this variable was declared at. Zero is
  // the outermost scope--parameters for a method, or the first local block in
  // top level code. One is the scope within that, etc.
  int depth;
} Local;

typedef struct {
  // The index of the local variable or upvalue being captured from the
  // enclosing function.
  uint8_t index;

  // Whether the captured variable is a local or upvalue in the enclosing
  // function.
  bool isLocal;
} Upvalue;

struct Compiler; // fwd decl

typedef struct Compiler {
  // The currently in scope local variables.
  struct Compiler *enclosing;
  Local locals[256];

  // The number of local variables currently in scope.
  int localCount;

  // The current level of block scope nesting. Zero is the outermost local
  // scope (global scope)
  int scopeDepth;
  bool hadError;
} Compiler;

/*typedef struct ClassCompiler {*/
  /*struct ClassCompiler *enclosing;*/

  /*Token name;*/
/*} ClassCompiler;*/

Compiler *current = NULL;
Token *curTok;
/*ClassCompiler *currentClass = NULL;*/

Chunk *compilingChunk;

static Chunk *currentChunk() {
  return compilingChunk;
}

static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, curTok ? curTok->line : 0);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitReturn() {
    //emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

static void emitNil() {
    emitByte(OP_NIL);
}

static void initCompiler(Compiler *compiler) {
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->enclosing = NULL;
    current = compiler;
    curTok = NULL;
}

static void endCompiler() {
    emitReturn();
}

static void error(const char *msg) {
    fprintf(stderr, "[Compile Error]: %s\n", msg);
    current->hadError = true;
}

// Adds a constant to the current chunk's constant pool and returns an index
// to it.
static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}

// Add constant to constant pool, return index to it
static uint8_t identifierConstant(Token* name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static void emitConstant(Value constant) {
    emitBytes(OP_CONSTANT, makeConstant(constant));
}

static void emitNode(Node *n);
static void emitChildren(Node *n) {
    Node *stmt = NULL;
    int i = 0;
    vec_foreach(n->children, stmt, i) {
        emitNode(stmt);
    }
}

static uint8_t declareVariable(Token *name) {
    if (current->scopeDepth == 0) {
        return identifierConstant(name);
    } else {
        return 0;
        // TODO: add local to table
    }
}

static void emitNode(Node *n) {
    curTok = &n->tok;
    switch (nodeKind(n)) {
    case STMTLIST_STMT:
    case EXPR_STMT: {
        emitChildren(n);
        return;
    }
    case BINARY_EXPR: {
        emitNode(n->children->data[0]);
        emitNode(n->children->data[1]);
        if (n->tok.type == TOKEN_PLUS) {
            emitByte(OP_ADD);
        } else if (n->tok.type == TOKEN_MINUS) {
            emitByte(OP_SUBTRACT);
        } else if (n->tok.type == TOKEN_STAR) {
            emitByte(OP_MULTIPLY);
        } else if (n->tok.type == TOKEN_SLASH) {
            emitByte(OP_DIVIDE);
        } else {
            error("invalid binary expr node");
        }
        return;
    }
    case UNARY_EXPR: {
        emitNode(n->children->data[0]);
        if (n->tok.type == TOKEN_MINUS) {
            emitByte(OP_NEGATE);
        } else if (n->tok.type == TOKEN_BANG) {
            emitByte(OP_NOT);
        } else {
            error("invalid unary expr node");
        }
        return;
    }
    case LITERAL_EXPR: {
        if (n->tok.type == TOKEN_NUMBER) {
            // TODO: handle error condition
            double d = strtod(tokStr(&n->tok), NULL);
            emitConstant(NUMBER_VAL(d));
        } else if (n->tok.type == TOKEN_STRING) {
            Token *name = &n->tok;
            emitConstant(OBJ_VAL(copyString(name->start+1, name->length-2)));
        } else if (n->tok.type == TOKEN_TRUE) {
            emitByte(OP_TRUE);
        } else if (n->tok.type == TOKEN_FALSE) {
            emitByte(OP_FALSE);
        } else {
            error("invalid literal expr node");
        }
        return;
    }
    case PRINT_STMT: {
        emitChildren(n);
        emitByte(OP_PRINT);
        return;
    }
    case VAR_STMT: {
        uint8_t varNameRef = declareVariable(&n->tok);
        if (n->children->length > 0) {
            emitChildren(n);
        } else {
            emitNil();
        }
        if (current->scopeDepth == 0) {
            emitBytes(OP_DEFINE_GLOBAL, varNameRef);
        } else {
            // TODO
        }
        return;
    }
    case VARIABLE_EXPR: {
        uint8_t varNameRef = identifierConstant(&n->tok);
        // TODO: find out which scope the var lives in
        OpCode getOp = OP_GET_LOCAL;
        if (current->scopeDepth == 0) {
            getOp = OP_GET_GLOBAL;
        }
        emitBytes(getOp, varNameRef);
        break;
    }
    case ASSIGN_EXPR: {
        // TODO: find out which scope the var lives in
        OpCode setOp = OP_SET_LOCAL;
        if (current->scopeDepth == 0) {
            setOp = OP_SET_GLOBAL;
        }
        Node *varNode = vec_first(n->children);
        uint8_t varNameRef = identifierConstant(&varNode->tok);
        emitNode(n->children->data[1]); // rval
        emitBytes(setOp, varNameRef);
        break;
    }
    default:
        error("invalid (unknown) node");
    }
}

int compile_src(char *src, Chunk *chunk, CompileErr *err) {
    initScanner(src);
    Compiler mainCompiler;
    initCompiler(&mainCompiler);
    Node *program = parse();
    if (CLOX_OPTION_T(parseOnly)) {
        return parser.hadError ? -1 : 0;
    }
    compilingChunk = chunk;
    emitNode(program);
    endCompiler();
    return parser.hadError ? -1 : 0;
}

int compile_file(char *fname, Chunk *chunk, CompileErr *err) {
    int fd = open(fname, O_RDONLY);
    if (fd == -1) {
        *err = COMPILE_ERR_ERRNO;
        return fd;
    }
    struct stat st;
    int res = fstat(fd, &st);
    if (res != 0) {
        *err = COMPILE_ERR_ERRNO;
        return res;
    }
    char *buf = malloc(st.st_size+1);
    if (buf == NULL) {
        *err = COMPILE_ERR_ERRNO;
        return -1;
    }
    memset(buf, 0, st.st_size+1);
    res = (int)read(fd, buf, st.st_size);
    if (res == -1) {
        *err = COMPILE_ERR_ERRNO;
        return res;
    }
    return compile_src(buf, chunk, err);
}
