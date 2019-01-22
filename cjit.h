#ifndef clox_cjit_h
#define clox_cjit_h

#include <stdio.h>
#include "common.h"
#include "chunk.h"
#include "nodes.h"
#include "value.h"
#include "object.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline Value jit_pop(Value **sp) {
    (*sp)--;
    return **sp;
}

static inline void jit_push(Value val, Value **sp) {
    **sp = val;
    (*sp)++;
}

//fprintf(stderr, "old ip: %p, new ip: %p\n", *ip, ((*ip)+n));
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
#define JIT_POP() jit_pop(sp)
#define JIT_NATIVE_SUCCESS ((Value)1)
#define JIT_NATIVE_ERROR ((Value)0)

int jitFunction(ObjFunction *func);
FILE *jitEmitIseqFile(Iseq *seq, Node *funcNode);
int jitEmitIseq(FILE *f, Iseq *seq, Node *funcNode);

#ifdef __cplusplus
}
#endif

#endif
