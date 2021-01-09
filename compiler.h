#ifndef clox_compiler_h
#define clox_compiler_h

#include "common.h"
#include "object.h"
#include "chunk.h"
#include "nodes.h"
#include "vec.h"
#include "value.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    COMPILE_SCOPE_MAIN=1,
    COMPILE_SCOPE_FUNCTION,
    COMPILE_SCOPE_IF,
    COMPILE_SCOPE_WHILE,
    COMPILE_SCOPE_FOREACH,
    COMPILE_SCOPE_FOR,
    COMPILE_SCOPE_TRY,
    COMPILE_SCOPE_IN,
    COMPILE_SCOPE_CLASS,
    COMPILE_SCOPE_MODULE,
    COMPILE_SCOPE_BLOCK,
} CompileScopeType;

typedef struct Scope {
    CompileScopeType type;
    struct Scope *parent;
    int line_start;
    int line_end;
    int bytecode_start;
    int bytecode_end;
} Scope;

// info on local variable slot lookup
typedef struct LocalVariable {
    struct ObjString *name;
    Scope *scope;
    int slot;
    int bytecode_declare_start;
} LocalVariable;

typedef struct Local {
  // The name of the local variable.
  Token name;

  // The depth in the scope chain that this variable was declared at. Zero is
  // the outermost scope--parameters for a method, or the first local block in
  // top level code. One is the scope within that, etc.
  int depth;
  bool isUpvalue;
  bool popOnScopeEnd;
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
  int localCountMax;

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

struct CallInfo; // fwd decl
#define ITER_FLAG_NONE 0
#define ITER_FLAG_STOP 1
typedef void (*BlockIterFunc)(int blkArgCount, Value *blkArgs, Value blkRet, struct CallInfo *cinfo, int *iterFlags);

#ifdef NAN_TAGGING
typedef Value uint64_t;
#else
typedef struct Value Value;
#endif

typedef struct CallInfo {
    Token nameTok;
    int argc;
    int numKwargs;
    bool usesSplat;
    Token kwargNames[LX_MAX_KWARGS];
    // for blocks
    ObjFunction *blockFunction; // lox block given as argument to fn
    ObjInstance *blockInstance; // from &blk, turned closure to block instance
    BlockIterFunc blockIterFunc; // lox iterators that work on top of #each
    volatile Value *blockIterRet; // return value from iterator
    volatile Value *blockArgsExtra;
    int blockArgsNumExtra;
    bool isYield;
} CallInfo;


typedef struct CompilerOpts {
    bool noOptimize; // default: false (optimize)
    bool noRemoveUnusedExpressions; // default: false (remove them)

    bool _inited; // private
} CompilerOpts;

extern CompilerOpts compilerOpts;

ObjFunction *compile_src(char *src, CompileErr *err);
ObjFunction *compile_node(Node *n, CompileErr *err);
ObjFunction *compile_eval_src(char *src, CompileErr *err, ObjInstance *instance, ObjFunction *func_in, uint32_t *ip_at);
ObjFunction *compile_binding_eval_src(char *src, CompileErr *err, ObjScope *scope);
ObjFunction *compile_file(char *fname, CompileErr *err);

void grayCompilerRoots(void);

const char *compileScopeName(CompileScopeType stype);

#ifdef __cplusplus
}
#endif

#endif
