#ifndef clox_cjit_h
#define clox_cjit_h

#include <stdio.h>
#include "common.h"
#include "chunk.h"
#include "nodes.h"
#include "value.h"
#include "object.h"
#include "runtime.h"
#include "options.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JIT_BINARY_OP(op, opcode, type) \
    do { \
      Value b = JIT_PEEK(0);\
      Value a = JIT_PEEK(1);\
      if (IS_NUMBER(a) && IS_NUMBER(b)) {\
          if (UNLIKELY(((opcode == OP_DIVIDE || opcode == OP_MODULO) && AS_NUMBER(b) == 0.00))) {\
              throwErrorFmt(lxErrClass, "Can't divide by 0");\
          }\
          JIT_POP();\
          JIT_PUSH_SWAP(NUMBER_VAL((type)AS_NUMBER(a) op (type)AS_NUMBER(b)));\
      } else if (IS_INSTANCE_LIKE(a)) {\
          ObjInstance *inst = AS_INSTANCE(a);\
          ObjString *methodName = methodNameForBinop(opcode);\
          Obj *callable = NULL;\
          if (methodName) {\
            callable = instanceFindMethod(inst, methodName);\
          }\
          if (UNLIKELY(!callable)) {\
              throwErrorFmt(lxNameErrClass, "Method %s#%s not found for operation '%s'", className(inst->klass), methodName->chars, #op);\
          }\
          callCallable(OBJ_VAL(callable), 1, true, NULL);\
      } else {\
          throwErrorFmt(lxTypeErrClass, "Binary operation type error, op=%s, lhs=%s, rhs=%s", #op, typeOfVal(a), typeOfVal(b));\
      }\
    } while (0)

static inline Value jit_pop(Value **sp) {
    VMExecContext *ctx = EC;
    (*sp)--;
    ctx->lastValue = *sp;
    vm.curThread->lastValue = ctx->lastValue;
    return **sp;
}

static inline Value jit_popn(Value **sp, int n) {
    VMExecContext *ctx = EC;
    (*sp)-=n;
    ctx->lastValue = *sp;
    vm.curThread->lastValue = ctx->lastValue;
    return **sp;
}

static inline void jit_push(Value val, Value **sp) {
    **sp = val;
    if (IS_OBJ(val)) {
        DBG_ASSERT(AS_OBJ(val)->type != OBJ_T_NONE);
        OBJ_SET_PUSHED_VM_STACK(AS_OBJ(val)); // for gen gc
    }
    (*sp)++;
}

static inline Value jit_peek(int n, Value **sp) {
    return *((*sp)-1-n);
}

static inline void jit_push_swap(Value val, Value **sp) {
    if (IS_OBJ(val)) {
        DBG_ASSERT(AS_OBJ(val)->type != OBJ_T_NONE);
        OBJ_SET_PUSHED_VM_STACK(AS_OBJ(val)); // for gen gc
    }
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
bool canJitFunction(ObjFunction *func);
FILE *jitEmitIseqFile(Iseq *seq, Node *funcNode, long funcNum);
int jitEmitIseq(FILE *f, Iseq *seq, Node *funcNode);

#ifdef __cplusplus
}
#endif

#endif
