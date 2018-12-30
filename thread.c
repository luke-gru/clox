#include <pthread.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"

ObjClass *lxThreadClass;
ObjClass *lxMutexClass;
ObjNative *nativeThreadInit = NULL;

ObjString *thKey;
ObjString *mutexKey;

void threadSetStatus(Value thread, ThreadStatus status) {
    LxThread *th = THREAD_GETHIDDEN(thread);
    th->status = status;
}
void threadSetId(Value thread, pthread_t tid) {
    LxThread *th = THREAD_GETHIDDEN(thread);
    th->tid = tid;
}

ThreadStatus threadGetStatus(Value thread) {
    LxThread *th = THREAD_GETHIDDEN(thread);
    return th->status;
}
pthread_t threadGetId(Value thread) {
    LxThread *th = THREAD_GETHIDDEN(thread);
    return th->tid;
}

static void enteredNewThread() {
    acquireGVLTid(pthread_self());
    Value thread = newThread();
    LxThread *th = THREAD_GETHIDDEN(thread);

    // set thread state from current (last) thread
    th->ec = vm.curThread->ec;
    VMExecContext *ctx = NULL; int ctxIdx = 0;
    // FIXME: figure out if it's safe to copy all this data (CallFrame too)
    vec_foreach(&vm.curThread->v_ecs, ctx, ctxIdx) {
        VMExecContext *newCtx = ALLOCATE(VMExecContext, 1);
        memcpy(newCtx, ctx, sizeof(VMExecContext));
        vec_push(&th->v_ecs, newCtx);
    }
    th->thisObj = vm.curThread->thisObj;
    th->lastValue = NULL;
    th->errInfo = NULL; // TODO: copy
    th->inCCall = vm.curThread->inCCall;
    th->cCallJumpBufSet = vm.curThread->cCallJumpBufSet;
    th->cCallThrew = false;
    th->returnedFromNativeErr = false;
    memcpy(&th->cCallJumpBuf, &vm.curThread->cCallJumpBuf, sizeof(jmp_buf));
    th->vmRunLvl = vm.curThread->vmRunLvl;
    th->lastSplatNumArgs = vm.curThread->lastSplatNumArgs;

    threadSetStatus(thread, THREAD_RUNNING);
    threadSetId(thread, pthread_self());
    vm.curThread = th;
    vec_push(&vm.threads, vm.curThread);
    // TODO: set other threads to STOPPED?
}

static void exitingThread() {
    vec_remove(&vm.threads, vm.curThread);
}

static void *runCallableInNewThread(void *arg) {
    ObjClosure *closure = arg;
    ASSERT(closure);
    enteredNewThread(); // acquires GVL
    THREAD_DEBUG(2, "in new thread");
    push(OBJ_VAL(closure));
    THREAD_DEBUG(2, "calling callable");
    callCallable(OBJ_VAL(closure), 0, false, NULL);
    exitingThread();
    THREAD_DEBUG(2, "exiting new thread");
    releaseGVL();
    return AS_OBJ(pop());
}

// usage: newThread(fun() { ... });
Value lxNewThread(int argCount, Value *args) {
    CHECK_ARITY("newThread", 1, 1, argCount);
    Value closure = *args;
    CHECK_ARG_BUILTIN_TYPE(closure, IS_CLOSURE_FUNC, "function", 1);
    ObjClosure *func = AS_CLOSURE(closure);
    pthread_t tnew;
    releaseGVL();
    if (pthread_create(&tnew, NULL, runCallableInNewThread, func) == 0) {
        THREAD_DEBUG(2, "created thread id %lu", (unsigned long)tnew);
        acquireGVL();
        return NUMBER_VAL((unsigned long)tnew);
    } else {
        // TODO: throw lxThreadErrClass
        throwErrorFmt(lxErrClass, "Error creating new thread");
    }
}

// usage: joinThread(t);
Value lxJoinThread(int argCount, Value *args) {
    CHECK_ARITY("joinThread", 1, 1, argCount);
    Value tidNum = *args;
    CHECK_ARG_BUILTIN_TYPE(tidNum, IS_NUMBER_FUNC, "number", 1);
    double num = AS_NUMBER(tidNum);
    THREAD_DEBUG(2, "Joining thread id %lu\n", (unsigned long)num);
    int ret = 0;
    releaseGVL();
    // blocking call until given thread ends execution
    if ((ret = pthread_join((pthread_t)num, NULL)) != 0) {
        THREAD_DEBUG(1, "Error joining thread: (ret=%d)", ret);
        // TODO: throw lxThreadErrClass
        throwErrorFmt(lxErrClass, "Error joining thread");
    }
    acquireGVL();
    return NIL_VAL;
}

static void LxThreadSetup(LxThread *th) {
    th->tid = pthread_self();
    th->status = THREAD_STOPPED;
    th->ec = NULL;
    vec_init(&th->v_ecs);
    th->thisObj = NULL;
    th->lastValue = NULL;
    th->hadError = false;
    th->errInfo = NULL;
    th->lastErrorThrown = NIL_VAL;
    th->inCCall = 0;
    th->cCallThrew = false;
    th->returnedFromNativeErr = false;
    memset(&th->cCallJumpBuf, 0, sizeof(jmp_buf));
    th->cCallJumpBufSet = false;
    th->vmRunLvl = 0;
    th->lastSplatNumArgs = -1;
}

Value lxThreadInit(int argCount, Value *args) {
    CHECK_ARITY("Thread#init", 1, 1, argCount);
    callSuper(0, NULL, NULL);
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxThread), NULL, NULL);
    LxThread *th = ALLOCATE(LxThread, 1);
    LxThreadSetup(th);
    internalObj->data = th;
    selfObj->internal = internalObj;
    // NOTE: use INTERN("th") here because thKey may not be initialized (can
    // be called during initVM
    tableSet(selfObj->hiddenFields, OBJ_VAL(INTERN("th")), OBJ_VAL(internalObj));
    return self;
}

typedef struct LxMutex {
    pthread_t owner;
    pthread_mutex_t lock;
} LxMutex;

void setupMutex(LxMutex *mutex) {
    mutex->owner = 0;
    pthread_mutex_init(&mutex->lock, NULL);
}

void lockMutex(LxMutex *mutex) {
    // NOTE: don't release GVL here, we need to block
    pthread_mutex_lock(&mutex->lock); // can block
    pthread_t tid = pthread_self();
    mutex->owner = tid;
}

void unlockMutex(LxMutex *mutex) {
    pthread_mutex_unlock(&mutex->lock);
    mutex->owner = 0;
}

static LxMutex *mutexGetHidden(Value mutex) {
    Value internal = getHiddenProp(mutex, mutexKey);
    ASSERT(IS_INTERNAL(internal));
    LxMutex *m = AS_INTERNAL(internal)->data;
    return m;
}

static Value lxMutexInit(int argCount, Value *args) {
    CHECK_ARITY("Mutex#init", 1, 1, argCount);
    callSuper(0, NULL, NULL);
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxMutex), NULL, NULL);
    LxMutex *mutex = ALLOCATE(LxMutex, 1);
    setupMutex(mutex);
    internalObj->data = mutex;
    selfObj->internal = internalObj;
    tableSet(selfObj->hiddenFields, OBJ_VAL(mutexKey), OBJ_VAL(internalObj));
    return self;
}

static Value lxMutexLock(int argCount, Value *args) {
    CHECK_ARITY("Mutex#lock", 1, 1, argCount);
    Value self = *args;
    LxMutex *m = mutexGetHidden(self);
    lockMutex(m);
    return self;
}

static Value lxMutexUnlock(int argCount, Value *args) {
    CHECK_ARITY("Mutex#unlock", 1, 1, argCount);
    Value self = *args;
    LxMutex *m = mutexGetHidden(self);
    unlockMutex(m);
    return self;
}

void Init_ThreadClass() {
    // class Thread
    ObjClass *threadClass = addGlobalClass("Thread", lxObjClass);
    lxThreadClass = threadClass;
    nativeThreadInit = addNativeMethod(threadClass, "init", lxThreadInit);

    ObjClass *mutexClass = addGlobalClass("Mutex", lxObjClass);
    lxMutexClass = mutexClass;
    addNativeMethod(mutexClass, "init", lxMutexInit);
    addNativeMethod(mutexClass, "lock", lxMutexLock);
    addNativeMethod(mutexClass, "unlock", lxMutexUnlock);

    thKey = INTERN("th");
    mutexKey = INTERN("mutex");
}
