#include <dlfcn.h>
#include <stdlib.h>
#include <errno.h>
#include "cjit.h"
#include "vm.h"
#include "object.h"

static int jumpNo = 0;
static int loopNo = 0;
static int isJitting = 0;
static Iseq *curIseq = NULL;

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
    fprintf(f, "  JIT_BINARY_OP(+, OP_ADD, double);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_SUBTRACT(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_SUBTRACT);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  JIT_BINARY_OP(-, OP_SUBTRACT, double);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_MULTIPLY(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_SUBTRACT);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  JIT_BINARY_OP(*, OP_MULTIPLY, double);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_DIVIDE(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_SUBTRACT);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  JIT_BINARY_OP(/, OP_DIVIDE, double);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_MODULO(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_MODULO);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  JIT_BINARY_OP(%%, OP_MODULO, double);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_BITOR(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_BITOR);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  JIT_BINARY_OP(|, OP_BITOR, int);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_BITAND(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_BITAND);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  JIT_BINARY_OP(&, OP_BITAND, int);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_BITXOR(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_BITXOR);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  JIT_BINARY_OP(^, OP_BITXOR, int);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_SHOVEL_L(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_SHOVEL_L);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  JIT_BINARY_OP(<<, OP_SHOVEL_L, int);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_SHOVEL_R(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_SHOVEL_R);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  JIT_BINARY_OP(>>, OP_SHOVEL_R, int);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_NEGATE(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_NEGATE);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value val = JIT_PEEK(0);\n"
    "  JIT_PUSH_SWAP(NUMBER_VAL(-AS_NUMBER(val)));\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_NOT(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_NOT);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value val = JIT_PEEK(0);\n"
    "  JIT_PUSH_SWAP(BOOL_VAL(!isTruthy(val)));\n"
    );
    fprintf(f, "}\n");
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
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_SET_CONST);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value constName = JIT_READ_CONSTANT();\n"
    "  Value val = JIT_PEEK(0);\n"
    "  if (th->v_crefStack.length > 0) {\n"
    "    Value ownerKlass = OBJ_VAL(vec_last(&th->v_crefStack));\n"
    "    addConstantUnder(AS_STRING(constName)->chars, val, ownerKlass);\n"
    "  } else {\n"
    "    tableSet(&vm.constants, constName, val);\n"
    "  }"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_GET_CONST_UNDER(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_GET_CONST_UNDER);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value klass = JIT_POP();\n"
    "  Value varName = JIT_READ_CONSTANT();\n"
    "  Value val;\n"
    "  if (IS_NIL(klass)) {\n"
    "    if (tableGet(&vm.constants, varName, &val)) {\n"
    "      JIT_PUSH(val);\n"
    "    } else {\n"
    "      throwErrorFmt(lxNameErrClass, \"Undefined constant '%%s'.\", AS_STRING(varName)->chars);\n"
    "    }\n"
    "  } else {\n"
    "    if (!IS_CLASS(klass) && !IS_MODULE(klass)) {\n"
    "    throwErrorFmt(lxTypeErrClass, \"Constants must be defined under classes/modules\");\n"
    "    }\n"
    "    if (tableGet(CLASSINFO(AS_CLASS(klass))->constants, varName, &val)) {\n"
    "      JIT_PUSH(val);\n"
    "    } else {\n"
    "      throwErrorFmt(lxNameErrClass, \"Undefined constant '%%s::%%s'.\", className(AS_CLASS(klass)), AS_STRING(varName)->chars);\n"
    "    }\n"
    "  }\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_CLOSURE(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_CLOSURE);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "Value funcVal = JIT_READ_CONSTANT();\n"
    "ASSERT(IS_FUNCTION(funcVal));\n"
    "ObjFunction *func = AS_FUNCTION(funcVal);\n"
    "ObjClosure *closure = newClosure(func, NEWOBJ_FLAG_NONE);\n"
    "JIT_PUSH(OBJ_VAL(closure));\n"
    "for (int i = 0; i < closure->upvalueCount; i++) {\n"
        "uint8_t isLocal = JIT_READ_BYTE();\n"
        "uint8_t index = JIT_READ_BYTE();\n"
        "if (isLocal) {\n"
            // Make an new upvalue to close over the parent's local variable.
            "closure->upvalues[i] = captureUpvalue(getFrame()->slots + index);\n"
        "} else {\n"
            // Use the same upvalue as the current call frame.
            "closure->upvalues[i] = getFrame()->closure->upvalues[index];\n"
        "}\n"
    "}\n"
    );
    fprintf(f, "}\n");
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
    "  Value instanceVal = JIT_PEEK(numArgs);\n"
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
    fprintf(f, "{\n"
    "  JIT_ASSERT_OPCODE(OP_GET_THIS);\n"
    "  INC_IP(1);\n"
    "  ASSERT(th->thisObj);\n"
    "  JIT_PUSH(OBJ_VAL(th->thisObj));\n"
    "}\n"
    );
    return 0;
}
static int jitEmit_GET_SUPER(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_RETURN(FILE *f, Insn *insn) {
    fprintf(f, "{\n"
    "  JIT_ASSERT_OPCODE(OP_RETURN);\n"
    "  INC_IP(1);\n"
    );
    fprintf(f, ""
    "  if (th->v_blockStack.length > 0) {\n"
    "    ObjString *key = INTERN(\"ret\");\n"
    "    JIT_POP();\n"
    "    Value ret;\n"
    "    if (th->lastValue) {\n"
    "      ret = *th->lastValue;\n"
    "    } else {\n"
    "      ret = NIL_VAL;\n"
    "    }\n"
    "    Value err = newError(lxContinueBlockErrClass, NIL_VAL);\n"
    "    setProp(err, key, ret);\n"
    "    throwError(err);\n"
    "  }\n"
    );
    fprintf(f, "  Value val = NIL_VAL;\n"
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
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_REGEX);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value reStr = JIT_READ_CONSTANT();\n"
    "  DBG_ASSERT(IS_STRING(reStr));\n"
    "  Value re;\n"
    "  if (tableGet(&vm.regexLiterals, reStr, &re)) {\n"
    "    JIT_PUSH(re);\n"
    "  } else {\n"
    "    re = compileRegex(AS_STRING(reStr));\n"
    "    GC_OLD(AS_OBJ(re));\n"
    "    objFreeze(AS_OBJ(re));\n"
    "    tableSet(&vm.regexLiterals, reStr, re);\n"
    "    JIT_PUSH(re);\n"
    "  }\n"
    "}\n"
    );
    return 0;
}
static int jitEmit_ITER(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_ITER);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value iterable = JIT_PEEK(0);\n"
    "  if (UNLIKELY(!isIterableType(iterable))) {\n"
    "    throwErrorFmt(lxTypeErrClass, \"Non-iterable value given to 'foreach' statement. Type found: %%s\",\n"
    "    typeOfVal(iterable));\n"
    "  }\n"
    "  Value iterator = createIterator(iterable);\n"
    "  DBG_ASSERT(isIterator(iterator));\n"
    "  DBG_ASSERT(isIterableType(peek(0)));\n"
    "  JIT_PUSH_SWAP(iterator);\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_ITER_NEXT(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_ITER_NEXT);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value iterator = JIT_PEEK(0);\n"
    "  ASSERT(isIterator(iterator));\n"
    "  Value next = iteratorNext(iterator);\n"
    "  ASSERT(!IS_UNDEF(next));\n"
    "  JIT_PUSH(next);\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_BLOCK_BREAK(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_BLOCK_BREAK);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value err = newError(lxBreakBlockErrClass, NIL_VAL);\n"
    "  throwError(err);\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_BLOCK_CONTINUE(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_BLOCK_CONTINUE);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value ret;\n"
    "  ObjString *key = INTERN(\"ret\");\n"
    "  if (th->lastValue) {\n"
          "ret = *th->lastValue;\n"
    "  } else {\n"
          "ret = NIL_VAL;\n"
    "  }\n"
    "  Value err = newError(lxContinueBlockErrClass, NIL_VAL);\n"
    "  setProp(err, key, ret);\n"
    "  throwError(err);\n" // blocks catch this, not propagated
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_BLOCK_RETURN(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_BLOCK_RETURN);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  ObjString *key = INTERN(\"ret\");\n"
    "  Value ret = JIT_PEEK(0);\n"
    "  Value err = newError(lxReturnBlockErrClass, NIL_VAL);\n"
    "  setProp(err, key, ret);\n"
    "  JIT_POP();\n"
    "  throwError(err);\n"
    );
    return 0;
}
static int jitEmit_TO_BLOCK(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_TO_BLOCK);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value func = JIT_PEEK(0);\n"
    "  if (UNLIKELY(!isCallable(func))) {\n"
    "    JIT_POP();\n"
    "    throwErrorFmt(lxTypeErrClass, \"Cannot use '&' operator on a non-function\");\n"
    "  }\n"
    "  JIT_PUSH_SWAP(newBlock(AS_OBJ(func)));\n"
    );
    fprintf(f, "}\n");
    return 0;
}

static int jitEmit_TRUE(FILE *f, Insn *insn) {
    fprintf(f, "JIT_ASSERT_OPCODE(OP_TRUE);\n");
    fprintf(f, "INC_IP(1);\n");
    fprintf(f, "JIT_PUSH(TRUE_VAL);\n");
    fprintf(f, "/* /OP_TRUE */\n");
    return 0;
}
static int jitEmit_FALSE(FILE *f, Insn *insn) {
    fprintf(f, "JIT_ASSERT_OPCODE(OP_FALSE);\n");
    fprintf(f, "INC_IP(1);\n");
    fprintf(f, "JIT_PUSH(FALSE_VAL);\n");
    fprintf(f, "/* /OP_FALSE */\n");
    return 0;
}
static int jitEmit_NIL(FILE *f, Insn *insn) {
    fprintf(f, "JIT_ASSERT_OPCODE(OP_NIL);\n");
    fprintf(f, "INC_IP(1);\n");
    fprintf(f, "JIT_PUSH(NIL_VAL);\n");
    fprintf(f, "/* /OP_NIL */\n");
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
    fprintf(f, "/* /OP_POP */\n");
    return 0;
}
static int jitEmit_POP_CREF(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_POP_N(FILE *f, Insn *insn) {
    fprintf(f, "JIT_ASSERT_OPCODE(OP_POP_N);\n");
    fprintf(f, "INC_IP(1);\n");
    fprintf(f, "JIT_POPN(JIT_READ_BYTE());\n");
    fprintf(f, "/* /OP_POP_N*/\n");
    return 0;
}

static int jitEmit_EQUAL(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_EQUAL);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value rhs = JIT_POP();\n"
    "  Value lhs = JIT_PEEK(0);\n"
    "  if (isValueOpEqual(lhs, rhs)) {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(true));\n"
    "  } else {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(false));\n"
    "  }\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_NOT_EQUAL(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_NOT_EQUAL);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value rhs = JIT_POP();\n"
    "  Value lhs = JIT_PEEK(0);\n"
    "  if (isValueOpEqual(lhs, rhs)) {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(false));\n"
    "  } else {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(true));\n"
    "  }\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_GREATER(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_GREATER);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value rhs = JIT_POP();\n"
    "  Value lhs = JIT_PEEK(0);\n"
    "  if (UNLIKELY(!canCmpValues(lhs, rhs, OP_GREATER))) {\n"
    "    JIT_POP();\n"
    "    throwErrorFmt(lxTypeErrClass,\n"
    "      \"Can only compare 2 numbers or 2 strings with '>', lhs=%%s, rhs=%%s\",\n"
    "      typeOfVal(lhs), typeOfVal(rhs));\n"
    "  }\n"
    "  if (cmpValues(lhs, rhs, OP_GREATER) == 1) {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(true));\n"
    "  } else {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(false));\n"
    "  }\n"
    "}\n"
    );
    return 0;
}
static int jitEmit_LESS(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_LESS);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value rhs = JIT_POP();\n"
    "  Value lhs = JIT_PEEK(0);\n"
    "  if (UNLIKELY(!canCmpValues(lhs, rhs, OP_LESS))) {\n"
    "    JIT_POP();\n"
    "    throwErrorFmt(lxTypeErrClass,\n"
    "      \"Can only compare 2 numbers or 2 strings with '>', lhs=%%s, rhs=%%s\",\n"
    "      typeOfVal(lhs), typeOfVal(rhs));\n"
    "  }\n"
    "  if (cmpValues(lhs, rhs, OP_LESS) == -1) {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(true));\n"
    "  } else {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(false));\n"
    "  }\n"
    "}\n"
    );
    return 0;
}
static int jitEmit_GREATER_EQUAL(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_GREATER_EQUAL);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value rhs = JIT_POP();\n"
    "  Value lhs = JIT_PEEK(0);\n"
    "  if (UNLIKELY(!canCmpValues(lhs, rhs, OP_GREATER_EQUAL))) {\n"
    "    JIT_POP();\n"
    "    throwErrorFmt(lxTypeErrClass,\n"
    "      \"Can only compare 2 numbers or 2 strings with '>=', lhs=%%s, rhs=%%s\",\n"
    "      typeOfVal(lhs), typeOfVal(rhs));\n"
    "  }\n"
    "  if (cmpValues(lhs, rhs, OP_GREATER_EQUAL) != -1) {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(true));\n"
    "  } else {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(false));\n"
    "  }\n"
    "}\n"
    );
    return 0;
}
static int jitEmit_LESS_EQUAL(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_LESS_EQUAL);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value rhs = JIT_POP();\n"
    "  Value lhs = JIT_PEEK(0);\n"
    "  if (UNLIKELY(!canCmpValues(lhs, rhs, OP_LESS_EQUAL))) {\n"
    "    JIT_POP();\n"
    "    throwErrorFmt(lxTypeErrClass,\n"
    "      \"Can only compare 2 numbers or 2 strings with '<=', lhs=%%s, rhs=%%s\",\n"
    "      typeOfVal(lhs), typeOfVal(rhs));\n"
    "  }\n"
    "  if (cmpValues(lhs, rhs, OP_LESS_EQUAL) != 1) {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(true));\n"
    "  } else {\n"
    "    JIT_PUSH_SWAP(BOOL_VAL(false));\n"
    "  }\n"
    "}\n"
    );
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
    jumpNo++;
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_JUMP_IF_TRUE);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value cond = JIT_POP();\n");
    fprintf(f, "  uint8_t ipOffset = JIT_READ_BYTE();\n");
    fprintf(f, ""
    "  if (isTruthy(cond)) {\n"
    "     DBG_ASSERT(ipOffset > 0);\n"
    "     *ip += (ipOffset-1);\n"
    "     goto jumpLabel%d;\n"
    "  }\n", jumpNo);
    fprintf(f, "}\n");
    insn->jumpNo = jumpNo;
    return 0;
}
static int jitEmit_JUMP_IF_FALSE_PEEK(FILE *f, Insn *insn) {
    jumpNo++;
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_JUMP_IF_FALSE_PEEK);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value cond = JIT_PEEK(0);\n");
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
static int jitEmit_JUMP_IF_TRUE_PEEK(FILE *f, Insn *insn) {
    jumpNo++;
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_JUMP_IF_FALSE_PEEK);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, "  Value cond = JIT_PEEK(0);\n");
    fprintf(f, "  uint8_t ipOffset = JIT_READ_BYTE();\n");
    fprintf(f, ""
    "  if (isTruthy(cond)) {\n"
    "     DBG_ASSERT(ipOffset > 0);\n"
    "     *ip += (ipOffset-1);\n"
    "     goto jumpLabel%d;\n"
    "  }\n", jumpNo);
    fprintf(f, "}\n");
    insn->jumpNo = jumpNo;
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
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_THROW);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value throwable = JIT_POP();\n"
    "  if (IS_STRING(throwable)) {\n"
    "    Value msg = throwable;\n"
    "    throwable = newError(lxErrClass, msg);\n"
    "  }\n"
    "  if (UNLIKELY(!IS_AN_ERROR(throwable))) {\n"
    "    throwErrorFmt(lxTypeErrClass, \"Tried to throw unthrowable value, must be a subclass of Error. \"\n"
    "      \"Type found: %%s\", typeOfVal(throwable));\n"
    "  }\n"
    "  throwError(throwable);\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_GET_THROWN(FILE *f, Insn *insn) {
    fprintf(f, "catchLabel%d:\n", (int)iseqInsnByteDiff(curIseq->insns, insn));
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_GET_THROWN);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value catchTblIdx = JIT_READ_CONSTANT();\n"
    "  ASSERT(IS_NUMBER(catchTblIdx));\n"
    "  double idx = AS_NUMBER(catchTblIdx);\n"
    "  CatchTable *tblRow = getCatchTableRow((int)idx);\n"
    "  if (UNLIKELY(!IS_AN_ERROR(tblRow->lastThrownValue))) { // bug\n"
    "    fprintf(stderr, \"Non-throwable found (BUG): %%s\\n\", typeOfVal(tblRow->lastThrownValue));\n"
    "    ASSERT(0);\n"
    "  }\n"
    "  JIT_PUSH(tblRow->lastThrownValue);\n"
    );
    fprintf(f, "}\n");
    return 0;
}

static int jitEmit_INDEX_GET(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_INDEX_GET);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value lval = JIT_PEEK(1);\n"
    "  ObjInstance *instance = AS_INSTANCE(lval);\n"
    "  Obj *method = instanceFindMethodOrRaise(instance, INTERNED(\"opIndexGet\", 10));\n"
    "  callCallable(OBJ_VAL(method), 1, true, NULL);\n"
    );
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_INDEX_SET(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_INDEX_SET);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value lval = JIT_PEEK(2);\n"
    "  ObjInstance *instance = AS_INSTANCE(lval);\n"
    "  Obj *method = instanceFindMethodOrRaise(instance, INTERNED(\"opIndexSet\", 10));\n"
    "  callCallable(OBJ_VAL(method), 2, true, NULL);\n"
    );
    fprintf(f, "}\n");
    return 0;
}

static int jitEmit_CHECK_KEYWORD(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  JIT_ASSERT_OPCODE(OP_CHECK_KEYWORD);\n");
    fprintf(f, "  INC_IP(1);\n");
    fprintf(f, ""
    "  Value kwMap = JIT_PEEK(0);\n"
    "  ASSERT(IS_T_MAP(kwMap));\n"
    "  uint8_t kwSlot = JIT_READ_BYTE();\n"
    "  uint8_t mapSlot = JIT_READ_BYTE();\n"
    "  (void)mapSlot;\n"
    "  if (IS_UNDEF(getFrame()->slots[kwSlot])) {\n"
    "    JIT_PUSH(BOOL_VAL(false));\n"
    "  } else {\n"
    "    JIT_PUSH(BOOL_VAL(true));\n"
    "  }\n"
    );
    fprintf(f, "}\n");
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
/*#ifndef NDEBUG*/
    /*fprintf(f, "fprintf(stderr, \"jit running op: %s (%d)\\n\");\n", opName((OpCode)code), code);*/
/*#endif*/
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

static void jitEmitCatchTable(FILE *f, Iseq *seq) {
    fprintf(f, ""
    "Chunk *ch = getFrame()->closure->function->chunk;\n"
    "int jumpRes = setjmp(getFrame()->jmpBuf);\n"
    "if (jumpRes == JUMP_SET) {\n"
    "  getFrame()->jmpBufSet = true;\n"
    "} else {\n"
    "  *ip = getFrame()->ip;\n"
    /*"  fprintf(stderr, \"catch ip diff: %%ld\\n\", *ip-ch->code);\n"*/
    "  switch (*ip-ch->code) {\n");
    CatchTable *curCatchTbl = seq->catchTbl;
    while (curCatchTbl) {
        fprintf(f, "    case %d: goto catchLabel%d;\n", curCatchTbl->itarget, curCatchTbl->itarget);
        curCatchTbl = curCatchTbl->next;
    }
    fprintf(f, "    default: ASSERT(0);\n");
    fprintf(f, "  }\n"
    "}\n"
    );
}

static int jitEmitFunctionEnter(FILE *f, Iseq *seq, Node *funcNode) {
    fprintf(f, "#include \"cjit_header.h\"\n\n");
    fprintf(f, "extern Value jittedFunc(LxThread *th, Value **sp, Value *slots, uint8_t **ip, Value *constantSlots);\n\n");
    fprintf(f, "Value jittedFunc(LxThread *th, Value **sp, Value *slots, uint8_t **ip, Value *constantSlots) {\n");

    if (seq->catchTbl) {
        jitEmitCatchTable(f, seq);
    }
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
    ASSERT(isJitting == 0);
    ASSERT(curIseq == NULL);
    ASSERT(!func->jitNative);
    ASSERT(func->iseq);
    ASSERT(func->funcNode);
    curIseq = func->iseq;
    isJitting++;
    FILE *f = jitEmitIseqFile(func->iseq, func->funcNode);
    isJitting--;
    curIseq = NULL;
    fclose(f);
    // TODO: use same C compiler that compiled clox, with same defines
    int res = system("gcc -std=c99 -fPIC -Wall -I. -I./vendor -D_GNU_SOURCE -DNAN_TAGGING -DCOMPUTED_GOTO -DLOX_JIT=1 -O2 -shared -o /tmp/loxjit.so /tmp/loxjit.c");
    if (res != 0) {
        fprintf(stderr, "Error during jit gcc:\n");
        exit(1);
    }
    void *dlHandle = dlopen("/tmp/loxjit.so", RTLD_NOW|RTLD_LOCAL);
    if (!dlHandle) {
        fprintf(stderr, "dlopen failed with: %s\n", dlerror());
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
