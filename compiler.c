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

typedef struct Compiler {
  // The currently in scope local variables.
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
/*ClassCompiler *currentClass = NULL;*/

Chunk *compilingChunk;

static Chunk *currentChunk() {
  return compilingChunk;
}

static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitReturn() {
    //emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

static void initCompiler(Compiler *compiler) {
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    current = compiler;
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

static void emitNode(Node *n) {
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
            error("invalid node");
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
            error("invalid node");
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
        } else {
            error("invalid node");
        }
        return;
    }
    default:
        error("invalid node");
    }
}

int compile_src(char *src, Chunk *chunk, CompileErr *err) {
    initScanner(src);
    Compiler mainCompiler;
    initCompiler(&mainCompiler);
    Node *program = parse();
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
