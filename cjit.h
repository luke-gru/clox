#ifndef clox_cjit_h
#define clox_cjit_h

#include <stdio.h>
#include "common.h"
#include "chunk.h"
#include "nodes.h"
#include "value.h"
#include "object.h"
#include "runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline Value jit_pop(Value **sp) {
    (*sp)--;
    return **sp;
}

static inline Value jit_popn(Value **sp, int n) {
    (*sp)-=n;
    return **sp;
}

static inline void jit_push(Value val, Value **sp) {
    **sp = val;
    (*sp)++;
}

static inline Value jit_peek(int n, Value **sp) {
    return *((*sp)-1-n);
}

static inline void jit_push_swap(Value val, Value **sp) {
    *(*sp-1) = val;
}

#define INC_IP(n) \
    (*ip)+=n

#define JIT_ASSERT_OPCODE(opcode) \
    if (opcode != **ip) {\
        fprintf(stderr, "Expected opcode %s (%d), got (%d)\n", #opcode, opcode, **ip);\
    }\
    ASSERT(opcode == **ip)

#define JIT_READ_BYTE() (*((*ip)++))
#define JIT_READ_CONSTANT() (constantSlots[JIT_READ_BYTE()])
#define JIT_PUSH(val) jit_push(val, sp)
#define JIT_PUSH_SWAP(val) jit_push_swap(val, sp)
#define JIT_POP() jit_pop(sp)
#define JIT_POPN(n) jit_popn(sp, n)
#define JIT_NATIVE_SUCCESS ((Value)1)
#define JIT_NATIVE_ERROR ((Value)0)
#define JIT_PEEK(n) jit_peek(n, sp)

int jitFunction(ObjFunction *func);
FILE *jitEmitIseqFile(Iseq *seq, Node *funcNode);
int jitEmitIseq(FILE *f, Iseq *seq, Node *funcNode);

#ifdef __cplusplus
}
#endif

#endif
