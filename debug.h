#ifndef clox_debug_h
#define clox_debug_h

#include <stdarg.h>
#include "common.h"
#include "chunk.h"
#include "object.h"
#include "vec.h"

typedef vec_t(ObjFunction*) vec_funcp_t;

void printDisassembledChunk(Chunk *chunk, const char *name);
int  printDisassembledInstruction(Chunk *chunk, int i, vec_funcp_t *funcs);

ObjString *disassembleChunk(Chunk *chunk);

char *opName(OpCode code);

extern void die(const char *fmt, ...);
#define ASSERT(expr) ((expr) ? (void)0 : die("assertion failure (%s:%d) in %s\n", __FILE__, __LINE__, __func__))
#define ASSERT_MEM(expr) ASSERT(expr)

#endif
