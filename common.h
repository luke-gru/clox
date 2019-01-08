#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define JUMP_SET 0
#define JUMP_PERFORMED 1

#define xstr(a) #a
#define QUOTE(x) xstr(x)

// generational GC, on by default
#ifndef GEN_GC
#define GEN_GC 1
#endif

#ifdef __GNUC__
#define LIKELY(x)       __builtin_expect((x),1)
#define UNLIKELY(x)     __builtin_expect((x),0)
#else
#define LIKELY(x) x
#define UNLIKELY(x) x
#endif

// NOTE: when adding/removing from here, add/remove from opName() function in debug.c!
typedef enum {
    #define OPCODE(name) OP_##name,
    #include "opcodes.h.inc"
    #undef OPCODE
} OpCode;

#endif
