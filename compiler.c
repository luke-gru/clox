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
#include "vm.h"

#ifdef NDEBUG
#define COMP_TRACE(...) (void(0))
#else
#define COMP_TRACE(...) compiler_trace_debug(__VA_ARGS__)
#endif

typedef struct Local {
  // The name of the local variable.
  Token name;

  // The depth in the scope chain that this variable was declared at. Zero is
  // the outermost scope--parameters for a method, or the first local block in
  // top level code. One is the scope within that, etc.
  int depth;
  bool isUpvalue;
} Local;

typedef struct Upvalue {
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
  Upvalue upvalues[256];

  // The number of local variables declared/defined in this scope (including
  // function parameters).
  int localCount;

  // The current level of block scope nesting. Zero is the outermost local
  // scope (global scope)
  int scopeDepth;
  bool hadError;
  bool emittedReturn; // has emitted at least 1 return for this function so far
  vec_int_t emittedReturnDepths;
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
    COMPILE_SCOPE_MODULE, // TODO
} CompileScopeType;

static const char *compileScopeName(CompileScopeType stype) {
    switch (stype) {
    case COMPILE_SCOPE_BLOCK: return "SCOPE_BLOCK";
    case COMPILE_SCOPE_FUNCTION: return "SCOPE_FUNCTION";
    case COMPILE_SCOPE_CLASS: return "SCOPE_CLASS";
    case COMPILE_SCOPE_MODULE: return "SCOPE_MODULE";
    default: {
        UNREACHABLE("invalid scope type: %d", stype);
    }
    }
}

Compiler *current = NULL;
ClassCompiler *currentClass = NULL;
Token *curTok = NULL;

typedef enum {
    VAR_GET,
    VAR_SET,
} VarOp;

static void compiler_trace_debug(const char *fmt, ...) {
    if (!CLOX_OPTION_T(traceCompiler)) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[COMP]: ");
    if (current) {
        fprintf(stderr, "(comp=%p,depth=%d): ", current, current->scopeDepth);
    }
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
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
    COMP_TRACE("pushScope: %s", compileScopeName(stype));
}

static void namedVariable(Token name, VarOp getSet);

static bool emittedReturnInScope(Compiler *comp) {
    if (comp->emittedReturn) {
        int idx = 0;
        // NOTE: add 1 here because the explicit return would be in the block scope
        // of the function, so it added a scopeDepth
        vec_find(&comp->emittedReturnDepths, comp->scopeDepth+1, idx);
        if (idx != -1) {
            return true;
        }
    }
    return false;
}

// returns nil, this is in case OP_RETURN wasn't emitted from an explicit
// `return` statement in a function.
static void emitReturn(Compiler *compiler) {
    ASSERT(compiler->type != FUN_TYPE_TOP_LEVEL);
    if (emittedReturnInScope(compiler)) {
        COMP_TRACE("Skipping emitting return");
        return;
    }
    COMP_TRACE("Emitting return");
    if (compiler->type == FUN_TYPE_INIT) {
        namedVariable(syntheticToken("this"), VAR_GET);
        emitByte(OP_RETURN);
    } else {
        emitByte(OP_NIL);
        emitByte(OP_RETURN);
    }
    compiler->emittedReturn = true;
    vec_push(&compiler->emittedReturnDepths, compiler->scopeDepth);
}

static void emitCloseUpvalue(void) {
    if (emittedReturnInScope(current)) {
        COMP_TRACE("Skipping emitting close upvalue (returned)");
        return;
    }
    COMP_TRACE("Emitting close upvalue");
    emitByte(OP_CLOSE_UPVALUE);
}

static void popScope(CompileScopeType stype) {
    COMP_TRACE("popScope: %s", compileScopeName(stype));
    while (current->localCount > 0 &&
            current->locals[current->localCount - 1].depth >= current->scopeDepth) {
        if (stype != COMPILE_SCOPE_CLASS) {
            COMP_TRACE("popScope emitting OP_POP");
            if (current->locals[current->localCount - 1].isUpvalue) {
                emitCloseUpvalue();
            } else {
                emitByte(OP_POP); // don't pop the non-pushed implicit 'super' in class scope
            }
        }
        current->localCount--;
    }
    if (stype == COMPILE_SCOPE_FUNCTION) {
        emitReturn(current);
    }
    current->scopeDepth--;
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

// Adds an upvalue to [compiler]'s function with the given properties. Does not
// add one if an upvalue for that variable is already in the list. Returns the
// index of the upvalue.
static int addUpvalue(Compiler *compiler, uint8_t index, bool isLocal) {
  // Look for an existing one.
  COMP_TRACE("Adding upvalue to COMP=%p, index: %d, isLocal: %s",
          compiler, index, isLocal ? "true" : "false");
  for (int i = 0; i < compiler->function->upvalueCount; i++) {
    Upvalue *upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) return i;
  }

  // If we got here, it's a new upvalue.
  if (compiler->function->upvalueCount == 256) {
    error("Too many closure variables in function.");
    return 0;
  }

  compiler->upvalues[compiler->function->upvalueCount].isLocal = isLocal;
  compiler->upvalues[compiler->function->upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

static bool identifiersEqual(Token *a, Token *b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

// returns -1 if local variable not found, otherwise returns slot index
// in the given compiler's locals table.
static int resolveLocal(Compiler *compiler, Token* name) {
  // Look it up in the local scopes. Look in reverse order so that the most
  // nested variable is found first and shadows outer ones.
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local *local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      return i;
    }
  }

  return -1;
}

// Attempts to look up [name] in the functions enclosing the one being compiled
// by [compiler]. If found, it adds an upvalue for it to this compiler's list
// of upvalues (unless it's already in there) and returns its index. If not
// found, returns -1.
//
// If the name is found outside of the immediately enclosing function, this
// will flatten the closure and add upvalues to all of the intermediate
// functions so that it gets walked down to this one.
static int resolveUpvalue(Compiler *compiler, Token *name) {
  COMP_TRACE("Resolving upvalue for variable '%s'", tokStr(name));
  // If we are at the top level, we didn't find it.
  if (compiler->enclosing == NULL) return -1;

  // See if it's a local variable in the immediately enclosing function.
  int local = resolveLocal(compiler->enclosing, name);
  if (local != -1) {
      COMP_TRACE("Upvalue variable '%s' found as local", tokStr(name));
    // Mark the local as an upvalue so we know to close it when it goes out of
    // scope.
    compiler->enclosing->locals[local].isUpvalue = true;
    return addUpvalue(compiler, (uint8_t)local, true);
  }

  // See if it's an upvalue in the immediately enclosing function. In other
  // words, if it's a local variable in a non-immediately enclosing function.
  // This "flattens" closures automatically: it adds upvalues to all of the
  // intermediate functions to get from the function where a local is declared
  // all the way into the possibly deeply nested function that is closing over
  // it.
  int upvalue = resolveUpvalue(compiler->enclosing, name);
  if (upvalue != -1) {
     COMP_TRACE("Upvalue variable '%s' found as non-local", tokStr(name));
    return addUpvalue(compiler, (uint8_t)upvalue, false);
  }

  // If we got here, we walked all the way up the parent chain and couldn't
  // find it.
   COMP_TRACE("Upvalue variable '%s' not found", tokStr(name));
  return -1;
}

static ObjFunction *endCompiler() {
    if (current->type == FUN_TYPE_TOP_LEVEL) {
        emitLeave();
    }
    vec_deinit(&current->emittedReturnDepths);
    ObjFunction *func = current->function;
    current = current->enclosing;
    return func;
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
  ASSERT(vm.inited);
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
  int arg = resolveLocal(current, &name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(current, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
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
    Token *fTok, /* if NULL, ftype must be FUN_TYPE_TOP_LEVEL */
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
    vec_init(&compiler->emittedReturnDepths);

    current = compiler;

    switch (ftype) {
    case FUN_TYPE_NAMED:
        current->function->name = copyString(
            tokStr(fTok), strlen(tokStr(fTok))
        );
        break;
    case FUN_TYPE_INIT:
    case FUN_TYPE_METHOD: {
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
    case FUN_TYPE_ANON:
    case FUN_TYPE_TOP_LEVEL:
        current->function->name = NULL;
        break;
    default:
        error("invalid function type %d", ftype);
    }

    // The first slot is always implicitly declared.
    Local* local = &current->locals[current->localCount++];
    local->depth = current->scopeDepth;
    local->isUpvalue = false;
    if (ftype == FUN_TYPE_METHOD || ftype == FUN_TYPE_INIT) {
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

/*static bool assignExprValueUnused(Node *assignNode) {*/
    /*Node *parent = assignNode->parent;*/
    /*NodeType nType = parent->type.type;*/
    /*int nKind = parent->type.kind;*/
    /*while (parent && nKind == GROUPING_EXPR) {*/
        /*parent = parent->parent;*/
        /*nType = parent->type.type;*/
        /*nKind = parent->type.kind;*/
    /*}*/
    /*if (nType == NODE_STMT) {*/
        /*return true;*/
    /*} else {*/
        /*return false;*/
    /*}*/
/*}*/

static void emitClass(Node *n) {
    uint8_t nameConstant = identifierConstant(&n->tok);
    ClassCompiler cComp;
    memset(&cComp, 0, sizeof(cComp));
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
    uint8_t funcIdx = makeConstant(OBJ_VAL(func));
    emitBytes(OP_CLOSURE, funcIdx);
    // Emit arguments for each upvalue to know whether to capture a local or
    // an upvalue.
    for (int i = 0; i < func->upvalueCount; i++) {
        emitByte(fCompiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(fCompiler.upvalues[i].index);
    }

    if (ftype != FUN_TYPE_ANON) {
        if (currentClass == NULL) {
            defineVariable(identifierConstant(&n->tok), false); // define function as global or local var
        } else {
            emitBytes(OP_METHOD, identifierConstant(&n->tok));
        }
    }
}

static void emitNode(Node *n) {
    if (current->hadError) return;
    curTok = &n->tok;
    switch (nodeKind(n)) {
    case STMTLIST_STMT:
    case GROUPING_EXPR: {
        emitChildren(n);
        return;
    }
    case EXPR_STMT: {
        emitChildren(n);
        emitByte(OP_POP);
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
        namedVariable(n->tok, VAR_GET);
        break;
    }
    case ASSIGN_EXPR: {
        emitNode(n->children->data[1]); // rval
        Node *varNode = vec_first(n->children);
        namedVariable(varNode->tok, VAR_SET);
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
            emitFunction(n, FUN_TYPE_NAMED);
        } else {
            FunctionType ftype = FUN_TYPE_METHOD;
            if (strcmp(tokStr(&n->tok), "init") == 0) {
                ftype = FUN_TYPE_INIT;
            }
            emitFunction(n, ftype);
        }
        break;
    }
    case ANON_FN_EXPR: {
        emitFunction(n, FUN_TYPE_ANON);
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
        if (current->emittedReturn) {
            int idx = 0;
            vec_find(&current->emittedReturnDepths, current->scopeDepth, idx);
            if (idx != -1) {
                COMP_TRACE("Skipping emitting explicit return");
                break;
            }
        }

        if (n->children->length > 0) {
            if (current->type == FUN_TYPE_INIT) {
                namedVariable(syntheticToken("this"), VAR_GET);
            } else {
                emitChildren(n);
            }
            emitByte(OP_RETURN);
            COMP_TRACE("Emitting explicit return (children)");
            current->emittedReturn = true;
            vec_push(&current->emittedReturnDepths, current->scopeDepth);
        } else {
            COMP_TRACE("Emitting explicit return (void)");
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
        Node *arg = NULL;
        int i = 0;
        emitNode(vec_first(n->children));
        vec_foreach(n->children, arg, i) {
            if (i == 0) continue; // callable expr is pushed last
            emitNode(arg);
        }
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
    case INDEX_GET_EXPR: {
        emitChildren(n);
        emitByte(OP_INDEX_GET);
        break;
    }
    case INDEX_SET_EXPR: {
        emitChildren(n);
        emitByte(OP_INDEX_SET);
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
    initCompiler(&mainCompiler, 0, FUN_TYPE_TOP_LEVEL, NULL, chunk);
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
