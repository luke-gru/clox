#ifndef clox_debug_h
#define clox_debug_h

#include <stdio.h>
#include <stdarg.h>
#include "common.h"
#include "chunk.h"
#include "object.h"
#include "vec.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NORETURN
#define NORETURN __attribute__((noreturn))
#endif


// For outputting ObjFunction chunks when printing bytecode
typedef vec_t(ObjFunction*) vec_funcp_t;

void printDisassembledChunk(FILE *f, Chunk *chunk, const char *name);
int  printDisassembledInstruction(FILE *f, Chunk *chunk, int i, vec_funcp_t *funcs);
// Relies on program being build with -rdynamic for seeing function names (see man 3 backtrace)
void printCBacktrace(void);

ObjString *disassembleChunk(Chunk *chunk);

const char *opName(OpCode code);

// Prints msg, exit(1)
NORETURN extern void die(const char *fmt, ...);
// Prints msg, prints C backtrace, exit(1)
NORETURN extern void diePrintCBacktrace(const char *fmt, ...);

#define ASSERT(expr) ((expr) ? (void)0 : diePrintCBacktrace("assertion failure (%s:%d) in %s\n", __FILE__, __LINE__, __func__))
#define ASSERT_MEM(expr) ASSERT(expr)
#define UNREACHABLE(...) do {\
    fprintf(stderr, "BUG [UNREACHABLE]: (%s:%d:%s)\n", __FILE__, __LINE__, __func__);\
    diePrintCBacktrace(__VA_ARGS__);\
    } while (0)

// FIXME: when throwing errors always longjmps, get rid of this guard
#if 0
#define UNREACHABLE_RETURN(ret) do {\
    UNREACHABLE("return reached!");\
    return ret;\
} while (0)
#else
#define UNREACHABLE_RETURN(ret) return ret
#endif

#ifndef NDEBUG
#define DBG_ASSERT(expr) ASSERT(expr)
#else
#define DBG_ASSERT(expr) (void)0
#endif

#ifdef __cplusplus
}
#endif

#endif
