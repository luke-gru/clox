#include <dlfcn.h>
#include <stdlib.h>
#include <errno.h>
#include "cjit.h"
#include "vm.h"
#include "object.h"

static int jumpNo = 0;
static int loopNo = 0;

static int jitEmit_CONSTANT(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_CONSTANT);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value constant = JIT_READ_CONSTANT();\n");
    /*fprintf(f, "  printValue(stdout, constant, true, -1);\n");*/
    /*fprintf(f, "  fprintf(stdout, \"\\n\");\n");*/
    fprintf(f, "  JIT_PUSH(constant);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_ADD(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_ADD);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value b = JIT_PEEK(0);\n");
    fprintf(f, "  Value a = JIT_PEEK(1);\n");
    fprintf(f, "  JIT_POP();\n");
    fprintf(f, "  JIT_PUSH_SWAP(NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b)));\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_SUBTRACT(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_SUBTRACT);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value b = JIT_PEEK(0);\n");
    fprintf(f, "  Value a = JIT_PEEK(1);\n");
    fprintf(f, "  JIT_POP();\n");
    fprintf(f, "  JIT_PUSH_SWAP(NUMBER_VAL(AS_NUMBER(a) - AS_NUMBER(b)));\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_MULTIPLY(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_SUBTRACT);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value b = JIT_PEEK(0);\n");
    fprintf(f, "  Value a = JIT_PEEK(1);\n");
    fprintf(f, "  JIT_POP();\n");
    fprintf(f, "  JIT_PUSH_SWAP(NUMBER_VAL(AS_NUMBER(a) * AS_NUMBER(b)));\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_DIVIDE(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_SUBTRACT);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value b = JIT_PEEK(0);\n");
    fprintf(f, "  Value a = JIT_PEEK(1);\n");
    fprintf(f, "  JIT_POP();\n");
    fprintf(f, "  JIT_PUSH_SWAP(NUMBER_VAL(AS_NUMBER(a) / AS_NUMBER(b)));\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_MODULO(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_BITOR(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_BITAND(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_BITXOR(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SHOVEL_L(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SHOVEL_R(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_NEGATE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_NOT(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_GET_LOCAL(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_GET_LOCAL);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  uint8_t slot = JIT_READ_BYTE();\n");
    fprintf(f, "  (void)JIT_READ_BYTE();\n");
    fprintf(f, "  JIT_PUSH(slots[slot]);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_SET_LOCAL(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_SET_LOCAL);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  uint8_t slot = JIT_READ_BYTE();\n");
    fprintf(f, "  (void)JIT_READ_BYTE();\n");
    fprintf(f, "  slots[slot] = JIT_PEEK(0);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_UNPACK_SET_LOCAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SET_GLOBAL(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_SET_GLOBAL);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value val = JIT_PEEK(0);\n");
    fprintf(f, "  Value varName = JIT_READ_CONSTANT();\n");
    fprintf(f, "  tableSet(&vm.globals, varName, val);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_GET_GLOBAL(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_GET_GLOBAL);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value varName = JIT_READ_CONSTANT();\n");
    fprintf(f, "  Value val;\n");
    fprintf(f, ""
    "  if (tableGet(&vm.globals, varName, &val)) {\n"
    "    JIT_PUSH(val);\n"
    "  } else if (tableGet(&vm.constants, varName, &val)) {\n"
    "    JIT_PUSH(val);\n"
    "  } else { ASSERT(0); /* TODO */ }\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_DEFINE_GLOBAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_CONST(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_GET_CONST);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value varName = JIT_READ_CONSTANT();\n");
    fprintf(f, "  Value val;\n");
    fprintf(f, "  ObjClass *cref = NULL;\n");
    fprintf(f, ""
    "  if (th->v_crefStack.length > 0) {\n"
    "    cref = TO_CLASS(vec_last(&th->v_crefStack));\n"
    "  }\n"
    "  if (findConstantUnder(cref, AS_STRING(varName), &val)) {\n"
    "    JIT_PUSH(val);\n"
    "  } else {\n" // TODO: handle constant not found
    "    ASSERT(0);\n"
    "  }\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_SET_CONST(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_CONST_UNDER(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_CLOSURE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_UPVALUE(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_GET_UPVALUE);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  uint8_t slot = JIT_READ_BYTE();\n"
    "  uint8_t varName = JIT_READ_BYTE();\n"
    "  (void)varName;\n"
    "  JIT_PUSH(*getFrame()->closure->upvalues[slot]->value);\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_SET_UPVALUE(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_SET_UPVALUE);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  uint8_t slot = JIT_READ_BYTE();\n"
    "  uint8_t varName = JIT_READ_BYTE();\n"
    "  *getFrame()->closure->upvalues[slot]->value = JIT_PEEK(0);\n"
    "  (void)varName;\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_CLOSE_UPVALUE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_PROP_GET(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_PROP_SET(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_METHOD(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_CLASS_METHOD(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GETTER(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SETTER(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_CALL(FILE *f, Insn *insn) {
    fprintf(f, "{\n"
    "  JIT_ASSERT_OPCODE(OP_CALL);\n"
    "  INC_IP(1);\n"
    "  uint8_t numArgs = JIT_READ_BYTE();\n"
    "  Value callableVal = JIT_PEEK(numArgs);\n"
    "  Value callInfoVal = JIT_READ_CONSTANT();\n"
    "  CallInfo *callInfo = internalGetData(AS_INTERNAL(callInfoVal));\n"
    "  callCallable(callableVal, numArgs, false, callInfo);\n"
    "}\n");
    return 0;
}
static int jitEmit_INVOKE(FILE *f, Insn *insn) {
    fprintf(f, "{\n"
    "  JIT_ASSERT_OPCODE(OP_INVOKE);\n"
    "  INC_IP(1);\n"
    "  Value methodName = JIT_READ_CONSTANT();\n"
    "  ObjString *mname = AS_STRING(methodName);\n"
    "  uint8_t numArgs = JIT_READ_BYTE();\n"
    "  Value callInfoVal = JIT_READ_CONSTANT();\n"
    "  CallInfo *callInfo = internalGetData(AS_INTERNAL(callInfoVal));\n"
    "  Value instanceVal = peek(numArgs);\n"
    "  ObjInstance *inst = AS_INSTANCE(instanceVal);\n"
    "  Obj *callable = instanceFindMethod(inst, mname);\n"
    "  callCallable(OBJ_VAL(callable), numArgs, true, callInfo);\n"
    "}\n");
    return 0;
}
static int jitEmit_SPLAT_ARRAY(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_THIS(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_SUPER(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_RETURN(FILE *f, Insn *insn) {
    fprintf(f, "{\n"
    "  JIT_ASSERT_OPCODE(OP_RETURN);\n"
    "  INC_IP(1);\n"
    "  Value val = NIL_VAL;\n"
    "  return val;\n"
    "}\n");
    return 0;
}
static int jitEmit_PRINT(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_PRINT);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value val = JIT_POP();\n");
    fprintf(f, "  printValue(stdout, val, true, -1);\n");
    fprintf(f, "  printf(\"\\n\");\n");
    fprintf(f, "}\n");
    return 0;
}

static int jitEmit_STRING(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_STRING);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value strLit = JIT_READ_CONSTANT();\n");
    fprintf(f, "  uint8_t isStatic = JIT_READ_BYTE();\n");
    fprintf(f, "  (void)isStatic;\n");
    fprintf(f, "  JIT_PUSH(OBJ_VAL(lxStringClass));\n");
    fprintf(f, "  JIT_PUSH(strLit);\n");
    fprintf(f, "  callCallable(JIT_PEEK(1), 1, false, NULL);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_ARRAY(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_ARRAY);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  uint8_t numEls = JIT_READ_BYTE();\n"
    "  Value aryVal = newArray();\n"
    "  hideFromGC(AS_OBJ(aryVal));\n"
    "  ValueArray *ary = &AS_ARRAY(aryVal)->valAry;\n"
    "  for (int i = 0; i < numEls; i++) {\n"
    "    Value el = JIT_POP();\n"
    "    writeValueArrayEnd(ary, el);\n"
    "    OBJ_WRITE(aryVal, el);\n"
    "  }\n"
    "  JIT_PUSH(aryVal);\n"
    "  unhideFromGC(AS_OBJ(aryVal));\n"
    "}\n"
    "");
    return 0;
}
static int jitEmit_DUPARRAY(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_DUPARRAY);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value ary = JIT_READ_CONSTANT();\n");
    fprintf(f, "  JIT_PUSH(arrayDup(ary));\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_DUPMAP(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_DUPMAP);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  JIT_PUSH(mapDup(JIT_READ_CONSTANT()));\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_MAP(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, ""
    "  JIT_ASSERT_OPCODE(OP_MAP);\n"
    "  INC_IP(1);\n"
    "  uint8_t numKeyVals = JIT_READ_BYTE();\n"
    "  Value mapVal = newMap();\n"
    "  hideFromGC(AS_OBJ(mapVal));\n"
    "  Table *map = AS_MAP(mapVal)->table;\n"
    "  for (int i = 0; i < numKeyVals; i+=2) {\n"
    "    Value key = JIT_POP();\n"
    "    Value val = JIT_POP();\n"
    "    tableSet(map, key, val);\n"
    "    OBJ_WRITE(mapVal, key);\n"
    "    OBJ_WRITE(mapVal, val);\n"
    "  }\n"
    "  JIT_PUSH(mapVal);\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_REGEX(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_ITER(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_ITER_NEXT(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_BLOCK_BREAK(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_BLOCK_CONTINUE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_BLOCK_RETURN(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_TO_BLOCK(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_TRUE(FILE *f, Insn *insn) {
    fprintf(f, "JIT_ASSERT_OPCODE(OP_TRUE);\n");
    fprintf(f, "INC_IP(1);\n");
    fprintf(f, "JIT_PUSH(TRUE_VAL);\n");
    return 0;
}
static int jitEmit_FALSE(FILE *f, Insn *insn) {
    fprintf(f, "JIT_ASSERT_OPCODE(OP_FALSE);\n");
    fprintf(f, "INC_IP(1);\n");
    fprintf(f, "JIT_PUSH(FALSE_VAL);\n");
    return 0;
}
static int jitEmit_NIL(FILE *f, Insn *insn) {
    fprintf(f, "JIT_ASSERT_OPCODE(OP_NIL);\n");
    fprintf(f, "INC_IP(1);\n");
    fprintf(f, "JIT_PUSH(NIL_VAL);\n");
    return 0;
}

static int jitEmit_AND(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_AND);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value rhs = JIT_POP();\n");
    // NOTE: we only check truthiness of rhs because lhs is
    // short-circuited (a JUMP_IF_FALSE is output in the bytecode for
    // the lhs).
    fprintf(f, "  JIT_PUSH_SWAP(isTruthy(rhs) ? rhs : BOOL_VAL(false));\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_OR(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_OR);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value rhs = JIT_POP();\n");
    fprintf(f, "  Value lhs = JIT_PEEK(0);\n");
    fprintf(f, "  JIT_PUSH_SWAP(isTruthy(lhs) || isTruthy(rhs) ? rhs : lhs);\n");
    fprintf(f, "}\n");
    return 0;
}

static int jitEmit_POP(FILE *f, Insn *insn) {
    fprintf(f, "JIT_ASSERT_OPCODE(OP_POP);\n");
    fprintf(f, "INC_IP(1);\n");
    fprintf(f, "JIT_POP();\n");
    return 0;
}
static int jitEmit_POP_CREF(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_POP_N(FILE *f, Insn *insn) {
    fprintf(f, "JIT_ASSERT_OPCODE(OP_POP_N);\n");
    fprintf(f, "INC_IP(1);\n");
    fprintf(f, "JIT_POPN(JIT_READ_BYTE());\n");
    return 0;
}

static int jitEmit_EQUAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_NOT_EQUAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GREATER(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_LESS(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_LESS);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value rhs = JIT_POP();\n");
    fprintf(f, "  Value lhs = JIT_PEEK(0);\n");
    fprintf(f, ""
    "  if (cmpValues(lhs, rhs, %d) == -1) {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(true));\n"
    "  } else {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(false));\n"
    "  }\n", insn->code);
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_GREATER_EQUAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_LESS_EQUAL(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_JUMP(FILE *f, Insn *insn) {
    jumpNo++;
    ASSERT(insn->jumpTo);
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_JUMP);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  uint8_t offset = JIT_READ_BYTE();\n");
    fprintf(f, "  *ip += (offset-1);\n");
    fprintf(f, "  goto jumpLabel%d;\n", jumpNo);
    fprintf(f, "}\n");
    insn->jumpNo = jumpNo;
    return 0;
}
static int jitEmit_JUMP_IF_FALSE(FILE *f, Insn *insn) {
    jumpNo++;
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_JUMP_IF_FALSE);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value cond = JIT_POP();\n");
    fprintf(f, "  uint8_t ipOffset = JIT_READ_BYTE();\n");
    fprintf(f, ""
    "  if (!isTruthy(cond)) {\n"
    "     DBG_ASSERT(ipOffset > 0);\n"
    "     *ip += (ipOffset-1);\n"
    "     goto jumpLabel%d;\n"
    "  }\n", jumpNo);
    fprintf(f, "}\n");
    insn->jumpNo = jumpNo;
    return 0;
}
static int jitEmit_JUMP_IF_TRUE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_JUMP_IF_FALSE_PEEK(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_JUMP_IF_TRUE_PEEK(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_LOOP(FILE *f, Insn *insn) {
    ASSERT(insn->jumpTo);
    ASSERT(insn->jumpTo->loopNo > 0);
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_LOOP);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  uint8_t ipOffset = JIT_READ_BYTE();\n");
    fprintf(f, "  *ip -= (ipOffset+2);\n");
    fprintf(f, "  goto loopLabel%d;\n", insn->jumpTo->loopNo);
    fprintf(f, "}\n");
    return 0;
}

static int jitEmit_CLASS(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SUBCLASS(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_MODULE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_IN(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_THROW(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_THROWN(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_INDEX_GET(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_INDEX_SET(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_CHECK_KEYWORD(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_LEAVE(FILE *f, Insn *insn) {
    fprintf(f, ""
    "JIT_ASSERT_OPCODE(OP_LEAVE);\n"
    "INC_IP(1);\n"
    "vm.exited = true;\n"
    "return JIT_NATIVE_SUCCESS;\n"
    );
    return 0;
}

static void jitEmitDebug(FILE *f, uint8_t code) {
#ifndef NDEBUG
    fprintf(f, "fprintf(stderr, \"jit running op: %s (%d)\\n\");\n", opName((OpCode)code), code);
#endif
}

static void jitEmitJumpLabel(FILE *f, Insn *insn) {
    if (insn->isJumpLabel) {
        ASSERT(insn->jumpedFrom);
        fprintf(f, "jumpLabel%d:\n", insn->jumpedFrom->jumpNo);
    }
}

static void jitEmitLoopLabel(FILE *f, Insn *insn) {
    if (insn->isLoopLabel) {
        loopNo++;
        insn->loopNo = loopNo;
        fprintf(f, "loopLabel%d:\n", insn->loopNo);
    }
}


static int jitEmitInsn(FILE *f, Insn *insn) {
#define OPCODE(opcode) case OP_##opcode : {\
    jitEmitDebug(f, insn->code);\
    jitEmitLoopLabel(f, insn);\
    int res = jitEmit_##opcode(f, insn);\
    jitEmitJumpLabel(f, insn);\
    return res;\
}

    switch (insn->code) {
#include "opcodes.h.inc"
    default:
        fprintf(stderr, "Unknown instruction to jit: %d\n", insn->code);
        return -1;

    }
#undef OPCODE
}

static int jitEmitFunctionEnter(FILE *f, Iseq *seq, Node *funcNode) {
    fprintf(f, "#include \"cjit_header.h\"\n");
    fprintf(f, "extern Value jittedFunc(LxThread *th, Value **sp, Value *slots, uint8_t **ip, Value *constantSlots);\n");
    fprintf(f, "Value jittedFunc(LxThread *th, Value **sp, Value *slots, uint8_t **ip, Value *constantSlots) {\n");
    return 0;
}

static int jitEmitFunctionLeave(FILE *f, Iseq *seq, Node *funcNode) {
    fprintf(f, "return JIT_NATIVE_SUCCESS;\n");
    fprintf(f, "}\n");
    return 0;
}

FILE *jitEmitIseqFile(Iseq *seq, Node *funcNode) {
    FILE *f = fopen("/tmp/loxjit.c", "w");
    jitEmitIseq(f, seq, funcNode);
    return f;
}

int jitEmitIseq(FILE *f, Iseq *seq, Node *funcNode) {
    int ret = 0;
    jumpNo = 0;
    loopNo = 0;
    jitEmitFunctionEnter(f, seq, funcNode);
    Insn *insn = seq->insns;
    while (insn) {
        if ((ret = jitEmitInsn(f, insn)) != 0) {
            return ret;
        }
        insn = insn->next;
    }
    jitEmitFunctionLeave(f, seq, funcNode);
    return ret;
}

int jitFunction(ObjFunction *func) {
    ASSERT(!func->jitNative);
    ASSERT(func->iseq);
    ASSERT(func->funcNode);
    FILE *f = jitEmitIseqFile(func->iseq, func->funcNode);
    fclose(f);
    int res = system("gcc -std=c99 -fPIC -Wall -I. -I./vendor -D_GNU_SOURCE -DNAN_TAGGING -DCOMPUTED_GOTO -O2 -shared -o /tmp/loxjit.so /tmp/loxjit.c");
    if (res != 0) {
        fprintf(stderr, "Error during jit gcc:\n");
        exit(1);
    }
    void *dlHandle = dlopen("/tmp/loxjit.so", RTLD_NOW|RTLD_LOCAL);
    if (!dlHandle) {
        fprintf(stderr, "dlopen failed with: %s\n", strerror(errno));
        exit(1);
    }
    void *dlSym = dlsym(dlHandle, "jittedFunc");
    if (!dlSym) {
        fprintf(stderr, "dlsym failed with: %s\n", dlerror());
        exit(1);
    }
    func->jitNative = (JitNative)dlSym;
    return 0;
}
