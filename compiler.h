#ifndef clox_compile_h
#define clox_compile_h

#include "common.h"
#include "object.h"
#include "chunk.h"
#include "nodes.h"
#include "vec.h"

#define LX_MAX_LOCALS 256
#define LX_MAX_UPVALUES 256
#define LX_MAX_KWARGS 8

typedef vec_t(uint8_t) vec_byte_t;

typedef enum {
    COMPILE_ERR_NONE = 1,
    COMPILE_ERR_SYNTAX,
    COMPILE_ERR_SEMANTICS,
    COMPILE_ERR_ERRNO,
} CompileErr;

typedef enum {
    FUN_TYPE_NAMED = 1,
    FUN_TYPE_ANON,
    FUN_TYPE_INIT,
    FUN_TYPE_METHOD,
    FUN_TYPE_GETTER,
    FUN_TYPE_SETTER,
    FUN_TYPE_CLASS_METHOD,
    // implementation detail, top-level is compiled as if it was a function
    FUN_TYPE_TOP_LEVEL,
    FUN_TYPE_BLOCK,
} FunctionType;

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
  bool isLocal; // is local variable in immediately enclosing scope
} Upvalue;

struct Compiler; // fwd decl

typedef struct Compiler {
  // The currently in scope local variables.
  struct Compiler *enclosing;
  ObjFunction *function; // function or top-level code object
  FunctionType type;
  Local locals[LX_MAX_LOCALS];
  Upvalue upvalues[LX_MAX_UPVALUES];

  // The number of local variables declared/defined in this scope (including
  // function parameters).
  int localCount;

  // The current level of block scope nesting. Zero is the outermost local
  // scope (global scope)
  int scopeDepth;

  Iseq iseq; // Generated instructions for the function
  Table constTbl;

  vec_void_t v_errMessages;
  bool hadError;
} Compiler;


typedef struct ClassCompiler {
  struct ClassCompiler *enclosing;
  Token name;
  bool hasSuperclass;
  bool isModule;
} ClassCompiler;


typedef enum {
    COMPILE_SCOPE_BLOCK = 1,
    COMPILE_SCOPE_FUNCTION,
    COMPILE_SCOPE_CLASS,
    COMPILE_SCOPE_IN,
    COMPILE_SCOPE_MODULE,
} CompileScopeType;

typedef struct CallInfo {
    Token nameTok;
    int argc;
    int numKwargs;
    bool usesSplat;
    Token kwargNames[LX_MAX_KWARGS];
    ObjFunction *block;
    ObjClosure *cachedBlock;
    bool isYield;
} CallInfo;

typedef struct CompilerOpts {
    bool noOptimize; // default: false (optimize)
    bool noRemoveUnusedExpressions; // default: false (remove them)

    bool _inited; // private
} CompilerOpts;

extern CompilerOpts compilerOpts;

Chunk *compile_src(char *src, CompileErr *err);
Chunk *compile_file(char *fname, CompileErr *err);

void grayCompilerRoots(void);

#endif
