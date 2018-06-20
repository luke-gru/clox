#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stddef.h>
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
  ObjFunction *function; // function or top-level code object
  FunctionType type;
  Local locals[256];

  // The number of local variables currently in scope.
  int localCount;

  // The current level of block scope nesting. Zero is the outermost local
  // scope (global scope)
  int scopeDepth;
  bool hadError;
  bool emittedReturn;
} Compiler;


typedef struct ClassCompiler {
  struct ClassCompiler *enclosing;

  Token name;
  bool hasSuperclass;
} ClassCompiler;

typedef enum {
    COMPILE_SCOPE_BLOCK = 1,
    COMPILE_SCOPE_FUNCTION,
    COMPILE_SCOPE_CLASS,
    COMPILE_SCOPE_MODULE,
} CompileScopeType;

Compiler *current = NULL;
ClassCompiler *currentClass = NULL;
Token *curTok = NULL;

typedef enum {
    VAR_GET,
    VAR_SET,
} VarOp;

static Token syntheticToken(const char *lexeme) {
    Token tok;
    tok.start = lexeme;
    tok.length = strlen(lexeme);
    return tok;
}

static Chunk *currentChunk() {
  return &current->function->chunk;
}

static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, curTok ? curTok->line : 0);
}

// blocks (`{}`) push new scopes
static void pushScope(CompileScopeType stype) {
    current->scopeDepth++;
}

static void namedVariable(Token name, VarOp getSet);

// returns nil, this is in case OP_RETURN wasn't emitted from an explicit
// `return` statement in a function.
static void emitReturn(Compiler *compiler) {
    ASSERT(compiler->type != TYPE_TOP_LEVEL);
    if (compiler->emittedReturn) return;
    if (compiler->type == TYPE_INITIALIZER) {
        namedVariable(syntheticToken("this"), VAR_GET);
        emitByte(OP_RETURN);
    } else {
        emitByte(OP_NIL);
        emitByte(OP_RETURN);
    }
    compiler->emittedReturn = true;
}

static void popScope(CompileScopeType stype) {
    current->scopeDepth--;
    while (current->localCount > 0 &&
            current->locals[current->localCount - 1].depth > current->scopeDepth) {
        emitByte(OP_POP);
        current->localCount--;
    }
    if (stype == COMPILE_SCOPE_FUNCTION) {
        emitReturn(current);
    }
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitNil() {
    emitByte(OP_NIL);
}

// exit from script
static void emitLeave() {
    emitByte(OP_LEAVE);
}

static ObjFunction *endCompiler() {
    if (current->type == TYPE_TOP_LEVEL) {
        emitLeave();
    }
    ObjFunction *func = current->function;
    current = current->enclosing;
    return func;
}

static void error(const char *format, ...) {
    va_list args;
    int line = curTok ? curTok->line : 0;
    va_start(args, format);
    fprintf(stderr, "[Compile Error]: ");
    if (line > 0) {
        fprintf(stderr, "(line: %d) ", line);
    }
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

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

// Add constant to constant pool from the token's lexeme, return index to it
static uint8_t identifierConstant(Token* name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

// emits a constant instruction with the given operand
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

// emit a jump (forwards) instruction, returns a pointer to the byte that needs patching
static int emitJump(OpCode jumpOp) {
    emitByte(jumpOp);
    // TODO: make the offset bigger than 1 byte!
    emitByte(0); // patched later (see patchJump)
    return currentChunk()->count-1; // index for later patch
}

// patch jump forwards instruction by given offset
// TODO: make the offset bigger than 1 byte!
static void patchJump(int topatch, int jumpoffset) {
    ASSERT(currentChunk()->code[topatch] == 0);
    if (jumpoffset == -1) {
        jumpoffset = currentChunk()->count - topatch - 1;
    }
    currentChunk()->code[topatch] = jumpoffset;
}

// Emit a jump backwards (loop) instruction from the current code count to offset `loopStart`
// TODO: make the offset bigger than 1 byte!
static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);

  int offset = (currentChunk()->count - loopStart + 1);
  if (offset > UINT8_MAX) error("Loop body too large.");

  emitByte(offset);
}

// adds local variable to current compiler's table, returns var slot
static int addLocal(Token name) {
    if (current->localCount >= UINT8_MAX) {
        error("Too many local variables");
        return -1;
    }
    Local local = {
        .name = name,
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

// returns -1 if local variable not found, otherwise returns slot index for
// current compiler.
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

// Returns argument to give to SET_LOCAL/SET_GLOBAL, an identifier index or
// a local slot index.
static int declareVariable(Token *name) {
    if (current->scopeDepth == 0) {
        return identifierConstant(name); // global variables are implicity declared
    } else {
        // See if a local variable with this name is already declared in this scope.
        for (int i = current->localCount - 1; i >= 0; i--) {
            Local* local = &current->locals[i];
            if (local->depth != -1 && local->depth < current->scopeDepth) break;
            if (identifiersEqual(name, &local->name)) {
                error("Variable with name '%s' already defined in this scope.", tokStr(name));
                return -1;
            }
        }
        return addLocal(*name);
    }
}


// emit GET/SET global or local for named variable
static void namedVariable(Token name, VarOp getSet) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(&name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }
  if (getSet == VAR_SET) {
    emitBytes(setOp, (uint8_t)arg);
  } else {
    emitBytes(getOp, (uint8_t)arg);
  }
}

// Initializes a new compiler for a function, and sets it as the `current`
// function compiler.
static void initCompiler(
    Compiler *compiler, // new compiler
    int scopeDepth,
    FunctionType ftype,
    Token *fTok, /* if NULL, ftype must be TYPE_TOP_LEVEL */
    Chunk *chunk /* if NULL, creates new chunk */
) {
    memset(compiler, 0, sizeof(*compiler));
    compiler->enclosing = current;
    compiler->localCount = 0;
    compiler->scopeDepth = scopeDepth;
    compiler->function = newFunction(chunk);
    hideFromGC((Obj*)compiler->function); // TODO: figure out way to unhide these functions on freeVM()
    compiler->type = ftype;
    compiler->hadError = false;
    compiler->emittedReturn = false;

    current = compiler;

    switch (ftype) {
    case TYPE_FUNCTION:
        current->function->name = copyString(
            tokStr(fTok), strlen(tokStr(fTok))
        );
        break;

    case TYPE_INITIALIZER:
    case TYPE_METHOD: {
        ASSERT(currentClass);
        char *className = tokStr(&currentClass->name);
        char *funcName = tokStr(fTok);
        size_t methodNameBuflen = strlen(className)+1+strlen(funcName)+1; // +1 for '.' in between
        char *methodNameBuf = calloc(methodNameBuflen, 1);
        ASSERT_MEM(methodNameBuf);
        strcpy(methodNameBuf, className);
        strncat(methodNameBuf, ".", 1);
        strcat(methodNameBuf, funcName);
        ObjString *methodName = copyString(methodNameBuf, strlen(methodNameBuf));
        current->function->name = methodName;
        break;
    }
    case TYPE_TOP_LEVEL:
        current->function->name = NULL;
        break;
    default:
        error("invalid function type %d", ftype);
    }

    // The first slot is always implicitly declared.
    Local* local = &current->locals[current->localCount++];
    local->depth = current->scopeDepth;
    if (ftype != TYPE_FUNCTION && ftype != TYPE_TOP_LEVEL) {
        // In a method, it holds the receiver, "this".
        local->name.start = "this";
        local->name.length = 4;
    } else {
        // In a function, it holds the function, but cannot be referenced, so has
        // no name.
        local->name.start = "";
        local->name.length = 0;
    }
}

// Define a declared variable
static void defineVariable(uint8_t arg, bool checkDecl) {
  if (current->scopeDepth == 0) {
    emitBytes(OP_DEFINE_GLOBAL, arg);
  } else {
    // Mark the given local as defined now (-1 is undefined, but declared)
    /*if (current->locals[arg].depth != -1 && checkDecl) {*/
        /*error("undeclared local variable [slot %d], scope depth: %d", arg, current->scopeDepth);*/
    /*}*/
    current->locals[arg].depth = current->scopeDepth;
  }
}


static void emitClass(Node *n) {
    uint8_t nameConstant = identifierConstant(&n->tok);
    ClassCompiler cComp;
    cComp.name = n->tok;
    Token *superClassTok = (Token*)nodeGetData(n);
    cComp.hasSuperclass = superClassTok != NULL;
    cComp.enclosing = currentClass;
    currentClass = &cComp;

    if (cComp.hasSuperclass) {
        pushScope(COMPILE_SCOPE_CLASS);
        // get the superclass
        namedVariable(*superClassTok, VAR_GET);
        // Store the superclass in a local variable named "super".
        addLocal(syntheticToken("super"));

        emitBytes(OP_SUBCLASS, nameConstant); // VM pops the superclass and gets the class name
    } else {
        emitBytes(OP_CLASS, nameConstant); // VM gets the class name
    }

    emitChildren(n); // block node with methods

    if (cComp.hasSuperclass) {
        popScope(COMPILE_SCOPE_CLASS);
    }

    defineVariable(nameConstant, false);
    currentClass = cComp.enclosing;
}

// emit function or method
static void emitFunction(Node *n, FunctionType ftype) {
    Compiler fCompiler;
    initCompiler(&fCompiler, current->scopeDepth,
        ftype, &n->tok, NULL);
    pushScope(COMPILE_SCOPE_FUNCTION); // this scope holds the local variable parameters

    ObjFunction *func = fCompiler.function;

    vec_nodep_t *params = (vec_nodep_t*)nodeGetData(n);
    ASSERT(params);
    Node *param = NULL; int i = 0;
    vec_foreach(params, param, i) {
        uint8_t localSlot = declareVariable(&param->tok);
        defineVariable(localSlot, true);
        func->arity++;
    }
    emitChildren(n); // the blockNode
    popScope(COMPILE_SCOPE_FUNCTION);
    func = endCompiler();

    // save the chunk as a constant in the parent (now current) chunk
    uint8_t nameArg = makeConstant(OBJ_VAL(func));
    emitBytes(OP_CONSTANT, nameArg);

    if (currentClass == NULL) {
        defineVariable(identifierConstant(&n->tok), false); // define function as global or local var
    } else {
        emitBytes(OP_METHOD, identifierConstant(&n->tok));
    }
}

static void emitNode(Node *n) {
    if (current->hadError) return;
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
            error("invalid binary expr node (token: %s)", tokStr(&n->tok));
        }
        return;
    }
    case LOGICAL_EXPR: {
        if (n->tok.type == TOKEN_AND) {
            emitNode(vec_first(n->children)); // lhs
            // false and "hi"
            int skipRhsJump = emitJump(OP_JUMP_IF_FALSE_PEEK);
            emitNode(vec_last(n->children)); // rhs
            emitByte(OP_AND);
            patchJump(skipRhsJump, -1);
        } else if (n->tok.type == TOKEN_OR) {
            emitNode(vec_first(n->children)); // lhs
            int skipRhsJump = emitJump(OP_JUMP_IF_TRUE_PEEK);
            emitNode(vec_last(n->children)); // rhs
            emitByte(OP_OR);
            patchJump(skipRhsJump, -1);
        } else {
            error("invalid logical expression node (token: %s)", tokStr(&n->tok));
            ASSERT(0);
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
            error("invalid unary expr node (token: %s)", tokStr(&n->tok));
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
        } else if (n->tok.type == TOKEN_NIL) {
            emitByte(OP_NIL);
        } else {
            error("invalid literal expr node (token: %s)", tokStr(&n->tok));
        }
        return;
    }
    case ARRAY_EXPR: {
        emitChildren(n);
        emitConstant(NUMBER_VAL(n->children->length));
        emitByte(OP_CREATE_ARRAY);
        return;
    }
    case IF_STMT: {
        emitNode(n->children->data[0]); // condition
        int ifJumpStart = emitJump(OP_JUMP_IF_FALSE);
        emitNode(n->children->data[1]); // then branch
        Node *elseNode = NULL;
        if (n->children->length > 2) {
            elseNode = n->children->data[2];
        }
        if (elseNode == NULL) {
            patchJump(ifJumpStart, -1);
        } else {
            patchJump(ifJumpStart, -1);
            emitNode(elseNode);
        }
        break;
    }
    case WHILE_STMT: {
        int loopStart = currentChunk()->count + 2;
        emitNode(vec_first(n->children)); // cond
        int whileJumpStart = emitJump(OP_JUMP_IF_FALSE);
        emitNode(n->children->data[1]); // while block
        emitLoop(loopStart);
        patchJump(whileJumpStart, -1);
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
    case FUNCTION_STMT: {
        if (currentClass == NULL) {
            emitFunction(n, TYPE_FUNCTION);
        } else {
            FunctionType ftype = TYPE_METHOD;
            if (strcmp(tokStr(&n->tok), "init") == 0) {
                ftype = TYPE_INITIALIZER;
            }
            emitFunction(n, ftype);
        }
        break;
    }
    case CLASS_STMT: {
        emitClass(n);
        break;
    }
    case PROP_ACCESS_EXPR: {
        emitChildren(n);
        emitBytes(OP_PROP_GET, identifierConstant(&n->tok));
        break;
    }
    case PROP_SET_EXPR: {
        emitChildren(n);
        emitBytes(OP_PROP_SET, identifierConstant(&n->tok));
        break;
    }
    case RETURN_STMT: {
        if (n->children->length > 0) {
            if (current->type == TYPE_INITIALIZER) {
                namedVariable(syntheticToken("this"), VAR_GET);
            } else {
                emitChildren(n);
            }
            emitByte(OP_RETURN);
            current->emittedReturn = true;
        } else {
            emitReturn(current);
        }
        break;
    }
    case THIS_EXPR: {
        namedVariable(syntheticToken("this"), VAR_GET);
        break;
    }
    case CALL_EXPR: {
        int nArgs = n->children->length-1;
        // arbitrary, but we don't want the VM op stack to blow by pushing a whole
        // bunch of arguments
        if (nArgs > 8) {
            error("too many arguments given to function (%d), maximum 8", nArgs);
            return;
        }
        emitChildren(n); // expression, arguments
        emitByte(OP_CALL);
        emitByte((uint8_t)nArgs);
        break;
    }
    case TRY_STMT: {
        Chunk *chunk = currentChunk();
        vec_int_t vjumps;
        vec_init(&vjumps);
        int ifrom = chunk->count;
        emitNode(n->children->data[0]); // try block
        int jumpToEnd = emitJump(OP_JUMP);
        vec_push(&vjumps, jumpToEnd);
        int ito = chunk->count;
        Node *catchStmt = NULL; int i = 0;
        if (n->children->length > 1) {
            vec_foreach(n->children, catchStmt, i) {
                if (i == 0) continue; // already emitted
                int itarget = chunk->count;
                Token classTok = vec_first(catchStmt->children)->tok;
                ObjString *className = copyString(tokStr(&classTok), strlen(tokStr(&classTok)));
                double catchTblIdx = (double)addCatchRow(
                    chunk, ifrom, ito,
                    itarget, OBJ_VAL(className)
                );
                pushScope(COMPILE_SCOPE_BLOCK);
                // given variable expression to bind to (Ex: (catch Error `err`))
                if (catchStmt->children->length > 2) {
                    uint8_t getThrownArg = makeConstant(NUMBER_VAL(catchTblIdx));
                    emitBytes(OP_GET_THROWN, getThrownArg);
                    Token varTok = catchStmt->children->data[1]->tok;
                    declareVariable(&varTok);
                    namedVariable(varTok, VAR_SET);
                }
                emitNode(vec_last(catchStmt->children)); // catch block
                ASSERT(chunk == currentChunk());
                int jumpStart = emitJump(OP_JUMP); // jump to end of try statement
                vec_push(&vjumps, jumpStart);
                popScope(COMPILE_SCOPE_BLOCK);
            }

            int jump = -1; int j = 0;
            vec_foreach(&vjumps, jump, j) {
                patchJump(jump, -1);
            }
        }
        vec_deinit(&vjumps);
        break;
    }
    case THROW_STMT: {
        emitChildren(n);
        emitByte(OP_THROW);
        break;
    }
    default:
        error("invalid (unknown) node. kind (%d) not implemented (tok=%s)",
              nodeKind(n), tokStr(&n->tok)
        );
    }
}

int compile_src(char *src, Chunk *chunk, CompileErr *err) {
    initScanner(src);
    Compiler mainCompiler;
    initCompiler(&mainCompiler, 0, TYPE_TOP_LEVEL, NULL, chunk);
    Node *program = parse();
    if (CLOX_OPTION_T(parseOnly)) {
        *err = parser.hadError ? COMPILE_ERR_SYNTAX :
            COMPILE_ERR_NONE;
        return parser.hadError ? -1 : 0;
    } else if (parser.hadError) {
        *err = COMPILE_ERR_SYNTAX;
        return -1;
    }
    emitNode(program);
    ObjFunction *prog = endCompiler();
    *chunk = prog->chunk; // copy
    if (CLOX_OPTION_T(debugBytecode)) {
        printDisassembledChunk(chunk, "Bytecode:");
    }
    if (mainCompiler.hadError) {
        *err = COMPILE_ERR_SEMANTICS;
        return -1;
    } else {
        *err = COMPILE_ERR_NONE;
        return 0;
    }
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
    char *buf = calloc(st.st_size+1, 1);
    ASSERT_MEM(buf);
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

void grayCompilerRoots(void) {
    Compiler *compiler = current;
    while (compiler != NULL) {
        grayObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
