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
#include "memory.h"

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

typedef enum {
    COMPILE_SCOPE_BLOCK = 1,
    COMPILE_SCOPE_FUNCTION,
    COMPILE_SCOPE_CLASS,
    COMPILE_SCOPE_MODULE,
} CompileScopeType;


/*typedef struct ClassCompiler {*/
  /*struct ClassCompiler *enclosing;*/

  /*Token name;*/
/*} ClassCompiler;*/

Compiler *current = NULL;
Token *curTok = NULL;
Chunk *compilingChunk = NULL;
/*ClassCompiler *currentClass = NULL;*/


static Chunk *currentChunk() {
  return compilingChunk;
}

static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, curTok ? curTok->line : 0);
}

static void pushScope(CompileScopeType stype) {
    if (stype == COMPILE_SCOPE_BLOCK) {
        current->scopeDepth++;
    } else {
        ASSERT(0); // TODO;
    }
}

static void popScope(CompileScopeType stype) {
    if (stype == COMPILE_SCOPE_BLOCK) {
        current->scopeDepth--;
        while (current->localCount > 0 &&
                current->locals[current->localCount - 1].depth > current->scopeDepth) {
            /*if (current->locals[current->localCount - 1].isUpvalue) {*/
                /*emitByte(OP_CLOSE_UPVALUE);*/
            /*} else {*/
                emitByte(OP_POP);
            /*}*/
            current->localCount--;
        }
    } else {
        ASSERT(0);
    }
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitReturn() {
    //emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

static void emitLeave() {
    emitByte(OP_LEAVE);
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
    if (current->scopeDepth == 0) {
        emitLeave();
    } else {
        emitReturn();
    }
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

// emit a jump instruction, return a pointer to the byte that needs patching
static uint8_t *emitJump(OpCode jumpOp) {
    emitByte(jumpOp);
    // TODO: make the offset bigger than 1 byte!
    emitByte(0);
    return currentChunk()->code + (currentChunk()->count-1);
}

// TODO: make the offset bigger than 1 byte!
static void patchJump(uint8_t *byteToPatch, uint8_t byte) {
    ASSERT(*byteToPatch == 0);
    *byteToPatch = byte;
}

// TODO: make the offset bigger than 1 byte!
static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);

  int offset = (currentChunk()->count - loopStart + 1);
  if (offset > UINT8_MAX) error("Loop body too large.");

  emitByte(offset);
}

// adds local variable, returns slot
static int addLocal(Token *name) {
    if (current->localCount >= UINT8_MAX) {
        error("Too many local variables");
        return -1;
    }
    Local local = {
        .name = *name,
        .depth = current->scopeDepth,
    };
    current->locals[current->localCount] = local;
    current->localCount++;
    return current->localCount-1;
}

static bool identifiersEqual(Token *a, Token *b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Token* name) {
  // Look it up in the local scopes. Look in reverse order so that the most
  // nested variable is found first and shadows outer ones.
  for (int i = current->localCount - 1; i >= 0; i--) {
    Local *local = &current->locals[i];
    if (identifiersEqual(name, &local->name)) {
      return i;
    }
  }

  return -1;
}

static int declareVariable(Token *name) {
    if (current->scopeDepth == 0) {
        return identifierConstant(name);
    } else {
        // See if a local variable with this name is already declared in this scope.
        for (int i = current->localCount - 1; i >= 0; i--) {
            Local* local = &current->locals[i];
            if (local->depth != -1 && local->depth < current->scopeDepth) break;
            if (identifiersEqual(name, &local->name)) {
                error("Variable with this name already declared in this scope.");
                return -1;
            }
        }
        return addLocal(name);
    }
}

static void emitNode(Node *n) {
    curTok = &n->tok;
    switch (nodeKind(n)) {
    case STMTLIST_STMT:
    case GROUPING_EXPR:
    case EXPR_STMT: {
        emitChildren(n);
        return;
    }
    case BINARY_EXPR: {
        emitChildren(n);
        if (n->tok.type == TOKEN_PLUS) {
            emitByte(OP_ADD);
        } else if (n->tok.type == TOKEN_MINUS) {
            emitByte(OP_SUBTRACT);
        } else if (n->tok.type == TOKEN_STAR) {
            emitByte(OP_MULTIPLY);
        } else if (n->tok.type == TOKEN_SLASH) {
            emitByte(OP_DIVIDE);
        } else if (n->tok.type == TOKEN_LESS) {
            emitByte(OP_LESS);
        } else if (n->tok.type == TOKEN_GREATER) {
            emitByte(OP_GREATER);
        } else {
            fprintf(stderr, "binary expr token: %s\n", tokStr(&n->tok));
            error("invalid binary expr node");
        }
        return;
    }
    // TODO: implement short-circuit semantics using jump
    case LOGICAL_EXPR: {
        emitChildren(n);
        if (n->tok.type == TOKEN_AND) {
            emitByte(OP_AND);
        } else if (n->tok.type == TOKEN_OR) {
            emitByte(OP_OR);
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
    case IF_STMT: {
        emitNode(n->children->data[0]); // condition
        uint8_t *afterThenOffPtr = emitJump(OP_JUMP_IF_FALSE);
        int startOffset = currentChunk()->count-1;
        emitNode(n->children->data[1]); // then branch
        Node *elseNode = NULL;
        if (n->children->length > 2) {
            elseNode = n->children->data[2];
        }
        uint8_t *jumpEndIfPtr = NULL;
        int jumpEndIfOffset = 0;
        if (elseNode != NULL) {
            jumpEndIfPtr = emitJump(OP_JUMP);
            jumpEndIfOffset = currentChunk()->count-1;
            int endOffset = currentChunk()->count-1;
            uint8_t offset = (uint8_t)(endOffset - startOffset);
            patchJump(afterThenOffPtr, offset);
            emitNode(elseNode);
        }
        int endOffset = currentChunk()->count-1;
        // TODO: allow bigger jumps (uint16_t (short) offsets)
        if (elseNode == NULL) {
            uint8_t offset = (uint8_t)(endOffset - startOffset);
            patchJump(afterThenOffPtr, offset);
        }
        if (jumpEndIfPtr != NULL) {
            uint8_t offset = (uint8_t)(endOffset - jumpEndIfOffset);
            patchJump(jumpEndIfPtr, offset);
        }
        break;
    }
    case WHILE_STMT: {
        int loopStart = currentChunk()->count + 2;
        emitNode(vec_first(n->children)); // cond
        uint8_t *afterLoopPtr = emitJump(OP_JUMP_IF_FALSE);
        emitNode(n->children->data[1]); // while block
        emitLoop(loopStart);
        int loopEnd = currentChunk()->count;
        int offset = (loopEnd-loopStart-2);
        patchJump(afterLoopPtr, offset);
        emitByte(OP_POP); // pop condition off stack
        break;
    }
    case PRINT_STMT: {
        emitChildren(n);
        emitByte(OP_PRINT);
        return;
    }
    case VAR_STMT: {
        int arg = declareVariable(&n->tok);
        if (arg == -1) return; // error already printed
        if (n->children->length > 0) {
            emitChildren(n);
        } else {
            emitNil();
        }
        if (current->scopeDepth == 0) {
            emitBytes(OP_DEFINE_GLOBAL, (uint8_t)arg);
        } else {
            emitBytes(OP_SET_LOCAL, (uint8_t)arg);
        }
        return;
    }
    case VARIABLE_EXPR: {
        uint8_t arg = identifierConstant(&n->tok);
        OpCode getOp = OP_GET_LOCAL;
        if (current->scopeDepth == 0) {
            getOp = OP_GET_GLOBAL;
        } else {
            int slot = resolveLocal(&n->tok);
            if (slot == -1) { // not a local variable
                 getOp = OP_GET_GLOBAL;
            } else {
                getOp = OP_GET_LOCAL;
                arg = (uint8_t)slot;
            }
        }
        emitBytes(getOp, arg);
        break;
    }
    case ASSIGN_EXPR: {
        Node *varNode = vec_first(n->children);
        OpCode setOp = OP_SET_LOCAL;
        int slot = -1;
        uint8_t arg;
        if (current->scopeDepth == 0) {
            setOp = OP_SET_GLOBAL;
        } else {
            slot = resolveLocal(&varNode->tok);
            if (slot == -1) {
                setOp = OP_SET_GLOBAL;
            } else {
                setOp = OP_SET_LOCAL;
            }
        }
        if (setOp == OP_SET_GLOBAL) {
            arg = identifierConstant(&varNode->tok);
        } else {
            arg = slot;
        }
        emitNode(n->children->data[1]); // rval
        emitBytes(setOp, arg);
        break;
    }
    case BLOCK_STMT: {
        pushScope(COMPILE_SCOPE_BLOCK);
        emitChildren(n); // 1 child, list of statements
        popScope(COMPILE_SCOPE_BLOCK);
        break;
    }
    default:
        fprintf(stderr, "node kind %d not implemented (tok=%s)\n",
            nodeKind(n), tokStr(&n->tok));
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
    if (CLOX_OPTION_T(debugBytecode)) {
        printDisassembledChunk(chunk, "Bytecode:");
    }
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
