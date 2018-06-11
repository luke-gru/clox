#ifndef clox_debug_h
#define clox_debug_h

#include <stdarg.h>
#include "common.h"
#include "chunk.h"

void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstruction(Chunk* chunk, int i);

extern void die(const char *fmt, ...);
#define ASSERT(expr) ((expr) ? (void)0 : die("assertion failure (%s:%d) in %s\n", __FILE__, __LINE__, __func__))
#define ASSERT_MEM(expr) ASSERT(expr)

#endif
