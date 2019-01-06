#include <pthread.h>
#include <unistd.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"

ObjClass *lxThreadClass;
ObjClass *lxMutexClass;
ObjNative *nativeThreadInit = NULL;

#define BLOCKING_REGION_CORE(exec) do { \
    GVL_UNLOCK_BEGIN(); {\
	    exec; \
    } \
    GVL_UNLOCK_END(); \
} while(0)

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

bool isOnlyThread() {
    if (vm.threads.length <= 1) {
        return true;
    }
    ObjInstance *thI; int thIdx = 0;
    vec_foreach(&vm.threads, thI, thIdx) {
        LxThread *found = THREAD_GETHIDDEN(OBJ_VAL(thI));
        if (found != vm.curThread && found->status != THREAD_ZOMBIE) {
            return false;
        } else {
            return true;
        }
    }
    return false;
}

static void LxThreadSetup(LxThread *th) {
    th->tid = pthread_self();
    th->status = THREAD_STOPPED;
    th->ec = NULL;
    vec_init(&th->v_ecs);
    th->openUpvalues = NULL;
    th->thisObj = NULL;
    vec_init(&th->v_blockStack);
    th->lastValue = NULL;
    th->hadError = false;
    th->errInfo = NULL;
    th->lastErrorThrown = NIL_VAL;
    th->errorToThrow = NIL_VAL;
    th->inCCall = 0;
    th->cCallThrew = false;
    th->returnedFromNativeErr = false;
    memset(&th->cCallJumpBuf, 0, sizeof(jmp_buf));
    th->cCallJumpBufSet = false;
    th->vmRunLvl = 0;
    th->mutexCounter = 0;
    th->lastSplatNumArgs = -1;
    vec_init(&th->stackObjects);
    pthread_mutex_init(&th->sleepMutex, NULL);
    pthread_cond_init(&th->sleepCond, NULL);
    th->opsRemaining = THREAD_OPS_UNTIL_SWITCH;
    th->exitStatus = 0;
    th->joined = false;
    th->detached = false;
}

static void LxThreadCleanup(LxThread *th) {
    vec_deinit(&th->stackObjects);
    vec_deinit(&th->v_blockStack);
    pthread_mutex_destroy(&th->sleepMutex);
    pthread_cond_destroy(&th->sleepCond);
}

typedef struct NewThreadArgs {
    ObjClosure *func;
    LxThread *th;
} NewThreadArgs;

// NOTE: vm.curThread is NOT the current thread here, and doesn't hold
// the GVL. We can't call ANY functions that call THREAD() in them, as they'll
// fail (phthread_self() tid is NOT in vm.threads vector until end of
// function).
static ObjInstance *newThreadSetup(LxThread *parentThread) {
    ASSERT(parentThread);
    THREAD_DEBUG(3, "New thread setup");
    ObjInstance *thInstance = newInstance(lxThreadClass);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxThread), NULL, NULL);
    thInstance->internal = internalObj;
    LxThread *th = ALLOCATE(LxThread, 1);
    LxThreadSetup(th);
    internalObj->data = th;

    // set thread state from current (last) thread
    VMExecContext *ctx = NULL; int ctxIdx = 0;
    vec_foreach(&parentThread->v_ecs, ctx, ctxIdx) {
        VMExecContext *newCtx = ALLOCATE(VMExecContext, 1);
        memcpy(newCtx, ctx, sizeof(VMExecContext));
        newCtx->stackTop = newCtx->stack + (ctx->stackTop-ctx->stack);
        newCtx->stackTop--; // for the two current stack objects that newThread() creates
        newCtx->stackTop--;
        newCtx->frameCount = 1;
        newCtx->lastValue = NULL;
        vec_push(&th->v_ecs, newCtx);
    }
    th->ec = vec_last(&th->v_ecs);
    th->thisObj = parentThread->thisObj;
    th->lastValue = NULL;
    th->errInfo = NULL; // TODO: copy
    th->inCCall = 0;
    th->cCallJumpBufSet = false;
    th->cCallThrew = false;
    th->returnedFromNativeErr = false;
    memset(&th->cCallJumpBuf, 0, sizeof(jmp_buf));
    th->vmRunLvl = 0;
    th->lastSplatNumArgs = -1;

    th->status = THREAD_READY;
    th->tid = -1; // unknown, not yet created
    vec_push(&vm.threads, thInstance);
    THREAD_DEBUG(3, "New thread setup done");
    return thInstance;
}

void exitingThread() {
    ASSERT(vm.curThread);
    vm.curThread->status = THREAD_ZOMBIE;
    vm.curThread->openUpvalues = NULL;
    LxThreadCleanup(vm.curThread);
}

static void *runCallableInNewThread(void *arg) {
    NewThreadArgs *tArgs = (NewThreadArgs*)arg;
    pthread_t tid = pthread_self();
    LxThread *th = tArgs->th;
    th->tid = tid;
    THREAD_DEBUG(2, "switching to newly created thread, acquiring lock %lu", th->tid);
    acquireGVL();
    th = vm.curThread;
    th->status = THREAD_RUNNING;
    THREAD_DEBUG(2, "in new thread %lu", th->tid);
    ObjClosure *closure = tArgs->func;
    ASSERT(closure);
    FREE(NewThreadArgs, tArgs);
    ASSERT(th);
    ASSERT(GVLOwner == tid);
    ASSERT(tid == pthread_self());
    ASSERT(vm.curThread->tid == pthread_self());
    push(OBJ_VAL(closure));
    unhideFromGC((Obj*)closure);
    if (vm.exited) {
        THREAD_DEBUG(2, "vm exited, quitting new thread %lu", pthread_self());
        releaseGVL();
        pthread_exit(NULL);
    }
    THREAD_DEBUG(2, "calling callable %lu", pthread_self());
    callCallable(OBJ_VAL(closure), 0, false, NULL);
    THREAD_DEBUG(2, "Exiting thread (returned) %lu", pthread_self());
    stopVM(0); // actually just exits the thread
}


// usage: newThread(fun() { ... });
Value lxNewThread(int argCount, Value *args) {
    CHECK_ARITY("newThread", 1, 1, argCount);
    Value closure = *args;
    CHECK_ARG_BUILTIN_TYPE(closure, IS_CLOSURE_FUNC, "function", 1);
    ObjClosure *func = AS_CLOSURE(closure);
    pthread_t tnew;
    ObjInstance *threadInst = newThreadSetup(vm.curThread);
    LxThread *th = (LxThread*)threadInst->internal->data;
    ASSERT(th);
    // Argument to pthread_create not called right away, only called when new
    // thread is switched to. This is why we malloc the thread argument memory
    // instead of using stack space, which would get corrupted.
    NewThreadArgs *thArgs = ALLOCATE(NewThreadArgs, 1);
    thArgs->func = func;
    hideFromGC((Obj*)func); // XXX: this is needed, because it's only pushed onto the stack when the new thread runs
    thArgs->th = th;
    releaseGVL();
    if (pthread_create(&tnew, NULL, runCallableInNewThread, thArgs) == 0) {
        acquireGVL();
        th->tid = tnew;
        THREAD_DEBUG(2, "created thread id %lu", (unsigned long)tnew);
        return OBJ_VAL(threadInst);
    } else {
        acquireGVL();
        THREAD_DEBUG(2, "Error making new thread, throwing\n");
        // TODO: throw lxThreadErrClass
        throwErrorFmt(lxErrClass, "Error creating new thread");
    }
}

// usage: joinThread(t);
Value lxJoinThread(int argCount, Value *args) {
    CHECK_ARITY("joinThread", 1, 1, argCount);
    Value threadVal = *args;
    CHECK_ARG_IS_A(threadVal, lxThreadClass, 1);
    LxThread *th = THREAD_GETHIDDEN(threadVal);
    ASSERT(th->tid != -1);
    ASSERT(th);
    THREAD_DEBUG(2, "Joining thread id %lu\n", th->tid);
    int ret = 0;

    releaseGVL();
    // blocking call until given thread ends execution
    if ((ret = pthread_join(th->tid, NULL)) != 0) {
        THREAD_DEBUG(1, "Error joining thread: (ret=%d), throwing", ret);
        acquireGVL();
        // TODO: throw lxThreadErrClass
        throwErrorFmt(lxErrClass, "Error joining thread");
    }
    acquireGVL();
    th->joined = true;
    THREAD_DEBUG(2, "Joined thread id %lu\n", th->tid);
    return NIL_VAL;
}

Value lxThreadMainStatic(int argCount, Value *args) {
    CHECK_ARITY("Thread.main", 1, 1, argCount);
    return OBJ_VAL(vec_first(&vm.threads));
}

Value lxThreadCurrentStatic(int argCount, Value *args) {
    CHECK_ARITY("Thread.current", 1, 1, argCount);
    ObjInstance *curThread = FIND_THREAD_INSTANCE(vm.curThread->tid);
    return OBJ_VAL(curThread);
}

Value lxThreadScheduleStatic(int argCount, Value *args) {
    CHECK_ARITY("Thread.schedule", 1, 1, argCount);
    releaseGVL();
    pthread_yield();
    acquireGVL();
    return NIL_VAL;
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
    return self;
}

static void threadSchedule(LxThread *th) {
    // TODO: wake thread up pre-emptively if sleeping or blocked on IO
    releaseGVL();
    pthread_cond_signal(&th->sleepCond);
    acquireGVL();
}

Value lxThreadThrow(int argCount, Value *args) {
    CHECK_ARITY("Thread#throw", 2, 2, argCount);
    Value self = *args;
    Value err = args[1];
    CHECK_ARG_IS_A(err, lxErrClass, 1);
    LxThread *th = THREAD_GETHIDDEN(self);
    Value ret = NIL_VAL;
    if (th->status == THREAD_STOPPED || th->status == THREAD_RUNNING) {
        if (IS_NIL(th->lastErrorThrown)) {
            th->errorToThrow = err;
        }
        threadSchedule(th);
    }
    return ret;
}

void threadDetach(LxThread *th) {
    ASSERT(th && th != vm.curThread);
    th->detached = true;
    pthread_detach(th->tid);
    vm.numDetachedThreads++;
}

Value lxThreadDetach(int argCount, Value *args) {
    CHECK_ARITY("Thread#detach", 1, 1, argCount);
    Value self = *args;
    LxThread *th = THREAD_GETHIDDEN(self);
    if (th == vm.mainThread) {
        return BOOL_VAL(false);
    }
    if (th->detached || th->status == THREAD_KILLED || th->status == THREAD_ZOMBIE) {
        return BOOL_VAL(false);
    }
    threadDetach(th);
    return BOOL_VAL(true);
}

typedef struct LxMutex {
    LxThread *owner;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int waiting;
} LxMutex;

void setupMutex(LxMutex *mutex) {
    mutex->owner = NULL;
    mutex->waiting = 0;
    pthread_mutex_init(&mutex->lock, NULL);
    pthread_cond_init(&mutex->cond, NULL);
}

static void lockFunc(LxMutex *mutex, LxThread *th) {
    pthread_mutex_lock(&mutex->lock); // can block
    mutex->waiting++;
    pthread_cond_wait(&mutex->cond, &mutex->lock);
    mutex->waiting--;
    th->mutexCounter++;
    THREAD_DEBUG(1, "Thread %lu LOCKED mutex", th->tid);
    mutex->owner = th;
    pthread_mutex_unlock(&mutex->lock); // can block
}

void lockMutex(LxMutex *mutex) {
    pthread_mutex_lock(&mutex->lock); // can block
    LxThread *th = THREAD();
    if (mutex->owner == NULL) {
        THREAD_DEBUG(1, "Thread %lu LOCKED mutex (no contention)", th->tid);
        ASSERT(mutex->waiting == 0);
        mutex->owner = th;
        th->mutexCounter++;
        pthread_mutex_unlock(&mutex->lock); // can block
        return;
    }
    pthread_mutex_unlock(&mutex->lock); // can block
    THREAD_DEBUG(1, "Thread %lu locking mutex (contention)", th->tid);
    while (mutex->owner != th) {
        BLOCKING_REGION_CORE({
            lockFunc(mutex, th);
        });
        if (mutex->owner == th) break;
        THREAD_DEBUG(1, "Thread %lu trying again...", th->tid);
    }
}

void unlockMutex(LxMutex *mutex) {
    pthread_mutex_lock(&mutex->lock);
    LxThread *th = THREAD();
    ASSERT(mutex->owner == th);
    mutex->owner = NULL;
    THREAD_DEBUG(1, "Thread %lu unlocking mutex...", th->tid);
    if (mutex->waiting > 0) {
        THREAD_DEBUG(1, "Thread %lu signaling waiter(s)...", th->tid);
        pthread_cond_signal(&mutex->cond);
    }
    th->mutexCounter--;
    THREAD_DEBUG(1, "Thread %lu UNLOCKED mutex", th->tid);
    pthread_mutex_unlock(&mutex->lock);
}

static LxMutex *mutexGetHidden(Value mutex) {
    ObjInternal *internal = AS_INSTANCE(mutex)->internal;
    LxMutex *m = (LxMutex*)internal->data;
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

    ObjClass *threadStatic = classSingletonClass(threadClass);
    addNativeMethod(threadStatic, "main", lxThreadMainStatic);
    addNativeMethod(threadStatic, "current", lxThreadCurrentStatic);
    addNativeMethod(threadStatic, "schedule", lxThreadScheduleStatic);

    nativeThreadInit = addNativeMethod(threadClass, "init", lxThreadInit);
    addNativeMethod(threadClass, "throw", lxThreadThrow);
    addNativeMethod(threadClass, "detach", lxThreadDetach);

    ObjClass *mutexClass = addGlobalClass("Mutex", lxObjClass);
    lxMutexClass = mutexClass;
    addNativeMethod(mutexClass, "init", lxMutexInit);
    addNativeMethod(mutexClass, "lock", lxMutexLock);
    addNativeMethod(mutexClass, "unlock", lxMutexUnlock);
}
