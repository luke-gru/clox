#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"
#include "compiler.h"

ObjClass *lxBlockClass = NULL;
ObjNative *nativeBlockInit = NULL;

typedef struct LxBlock {
    ObjClosure *closure;
} LxBlock;

static void markInternalBlock(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    LxBlock *blk = internal->data;
    grayObject((Obj*)blk->closure);
}

static LxBlock *blockGetHidden(Value block) {
    return AS_INTERNAL(getHiddenProp(block, INTERN("blk")))->data;
}

static Value lxBlockInit(int argCount, Value *args) {
    CHECK_ARITY("Block#init", 2, 2, argCount);
    Value self = args[0];
    Value closureVal = args[1];
    ObjClosure *closure = AS_CLOSURE(closureVal);
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, 0, markInternalBlock, NULL);
    LxBlock *blk = ALLOCATE(LxBlock, 1);
    blk->closure = closure;
    internalObj->data = blk;
    internalObj->dataSz = sizeof(LxBlock);
    tableSet(selfObj->hiddenFields, OBJ_VAL(INTERN("blk")), OBJ_VAL(internalObj));
    selfObj->internal = internalObj;
    return self;
}

static Value lxBlockYield(int argCount, Value *args) {
    CHECK_ARITY("Block#yield", 1, -1, argCount);
    Value self = *args;
    LxBlock *blk = blockGetHidden(self);
    Value callable = OBJ_VAL(blk->closure);
    push(callable);
    for (int i = 1; i < argCount; i++) {
        push(args[i]);
    }
    int status = 0;
    ObjFunction *func = blk->closure->function;
    THREAD()->curBlock = blk->closure->function;
    SETUP_BLOCK(func, status, THREAD()->errInfo, THREAD()->lastBlock)
    if (status == TAG_NONE) {
    } else if (status == TAG_RAISE) {
        ObjInstance *errInst = AS_INSTANCE(THREAD()->lastErrorThrown);
        ASSERT(errInst);
        if (errInst->klass == lxBreakBlockErrClass) {
            return NIL_VAL;
        } else if (errInst->klass == lxContinueBlockErrClass) { // continue
            return getProp(THREAD()->lastErrorThrown, INTERN("ret"));
        } else if (errInst->klass == lxReturnBlockErrClass) {
            return getProp(THREAD()->lastErrorThrown, INTERN("ret"));
        } else {
            throwError(THREAD()->lastErrorThrown);
        }
    }
    callCallable(callable, argCount-1, false, NULL);
    UNREACHABLE("block didn't longjmp?"); // blocks should always longjmp out
}

void Init_BlockClass() {
    lxBlockClass = addGlobalClass("Block", lxObjClass);
    nativeBlockInit = addNativeMethod(lxBlockClass, "init", lxBlockInit);
    addNativeMethod(lxBlockClass, "yield", lxBlockYield);
}
