#ifndef clox_compile_h
#define clox_compile_h

#include "common.h"
#include "object.h"
#include "chunk.h"

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
    FUN_TYPE_TOP_LEVEL
} FunctionType;

typedef struct CompilerOpts {
    bool noOptimize; // default: false (optimize)
    bool noRemoveUnusedExpressions; // default: false (remove them)

    bool _inited; // private
} CompilerOpts;

extern CompilerOpts compilerOpts;

int compile_src(char *src, Chunk *chunk, CompileErr *err);
int compile_file(char *fname, Chunk *chunk, CompileErr *err);

void grayCompilerRoots(void);

#endif
