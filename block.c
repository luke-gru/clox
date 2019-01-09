#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"
#include "compiler.h"

ObjClass *lxBlockClass = NULL;
ObjNative *nativeBlockInit = NULL;

typedef struct LxBlock {
    // types of callable objects that can be coerced into blocks with &
    // are classes, closures, native functions (any callable)
    Obj *callable;
} LxBlock;

static void markInternalBlock(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    LxBlock *blk = internal->data;
    grayObject((Obj*)blk->callable);
}

static inline LxBlock *blockGetHidden(Value block) {
    return (LxBlock*) AS_INSTANCE(block)->internal->data;
}

Obj *blockCallable(Value block) {
    LxBlock *blk = blockGetHidden(block);
    return blk->callable;
}

Obj *blockCallableBlock(Value block) {
    LxBlock *blk = blockGetHidden(block);
    Obj *callable = blk->callable;
    if (callable->type == OBJ_T_CLOSURE) {
        return (Obj*)(((ObjClosure*)callable)->function);
    } else {
        return callable;
    }
}

ObjInstance *getBlockArg(CallFrame *frame) {
    if (frame->callInfo) {
        return frame->callInfo->blockInstance;
    } else {
        return NULL;
    }
}

static Value lxBlockInit(int argCount, Value *args) {
    CHECK_ARITY("Block#init", 2, 2, argCount);
    Value self = args[0];
    Value callableVal = args[1];
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, 0, markInternalBlock, NULL, NEWOBJ_FLAG_NONE);
    LxBlock *blk = ALLOCATE(LxBlock, 1);
    blk->callable = AS_OBJ(callableVal);
    internalObj->data = blk;
    internalObj->dataSz = sizeof(LxBlock);
    selfObj->internal = internalObj;
    return self;
}

static Value lxBlockYield(int argCount, Value *args) {
    CHECK_ARITY("Block#yield", 1, -1, argCount);
    Value self = *args;
    LxBlock *blk = blockGetHidden(self);
    Value callable = OBJ_VAL(blk->callable);
    push(callable);
    for (int i = 1; i < argCount; i++) {
        push(args[i]);
    }
    volatile int status = 0;
    Obj *block = blockCallableBlock(self);
    volatile LxThread *th = THREAD();
    volatile BlockStackEntry *bentry = NULL;
    SETUP_BLOCK(block, bentry, status, THREAD()->errInfo)
    if (status == TAG_NONE) {
    } else if (status == TAG_RAISE) {
        ObjInstance *errInst = AS_INSTANCE(th->lastErrorThrown);
        ASSERT(errInst);
        if (errInst->klass == lxBreakBlockErrClass) {
            return NIL_VAL;
        } else if (errInst->klass == lxContinueBlockErrClass) { // continue
            return getProp(THREAD()->lastErrorThrown, INTERN("ret"));
        } else if (errInst->klass == lxReturnBlockErrClass) {
            return getProp(THREAD()->lastErrorThrown, INTERN("ret"));
        } else {
            throwError(th->lastErrorThrown);
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
