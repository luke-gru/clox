#ifndef clox_compile_h
#define clox_compile_h

#include "common.h"
#include "object.h"
#include "chunk.h"

typedef enum {
    COMPILE_ERR_NONE,
    COMPILE_ERR_SYNTAX,
    COMPILE_ERR_SEMANTICS,
    COMPILE_ERR_ERRNO,
} CompileErr;

typedef enum {
  TYPE_FUNCTION,
  TYPE_INITIALIZER,
  TYPE_METHOD,
  TYPE_TOP_LEVEL
} FunctionType;

int compile_src(char *src, Chunk *chunk, CompileErr *err);
int compile_file(char *fname, Chunk *chunk, CompileErr *err);

void grayCompilerRoots(void);

#endif
