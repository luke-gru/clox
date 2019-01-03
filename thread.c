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
    th->curBlock = NULL;
    th->lastBlock = NULL; // this field for convenience, not necessary if we track it somewhere else
    th->outermostBlock = NULL;
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
    th->mutexCounter = 0;
    th->lastSplatNumArgs = -1;
    vec_init(&th->stackObjects);
    pthread_mutex_init(&th->sleepMutex, NULL);
    pthread_cond_init(&th->sleepCond, NULL);
    th->opsRemaining = THREAD_OPS_UNTIL_SWITCH;
}

static void LxThreadCleanup(LxThread *th) {
    vec_deinit(&th->stackObjects);
    pthread_mutex_destroy(&th->sleepMutex);
    pthread_cond_destroy(&th->sleepCond);
}

// NOTE: vm.curThread is NOT the current thread here, and doesn't hold
// the GVL. We can't call ANY functions that call THREAD() in them, as they'll
// fail (phthread_self() tid is NOT in vm.threads vector until end of
// function).
static void newThreadSetup(LxThread *parentThread) {
    ASSERT(parentThread);
    THREAD_DEBUG(3, "New thread setup");
    ObjInstance *thInstance = newInstance(lxThreadClass);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxThread), NULL, NULL);
    thInstance->internal = internalObj;
    LxThread *th = ALLOCATE(LxThread, 1);
    LxThreadSetup(th);
    internalObj->data = th;
    tableSet(thInstance->hiddenFields, OBJ_VAL(INTERN("th")), OBJ_VAL(internalObj));

    // set thread state from current (last) thread
    VMExecContext *ctx = NULL; int ctxIdx = 0;
    // FIXME: figure out if it's safe to copy all this data (CallFrame too)
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
    /*fprintf(stderr, "NEW VM STACK\n");*/
    /*printVMStack(stderr, th);*/
    /*fprintf(stderr, "/NEW VM STACK\n");*/
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
}

static void exitingThread() {
    ASSERT(vm.curThread);
    vm.curThread->status = THREAD_ZOMBIE;
    vm.curThread->openUpvalues = NULL;
    int idx = 0;
    vec_find(&vm.threads, FIND_THREAD_INSTANCE(vm.curThread->tid), idx);
    ASSERT(idx > -1);
    vec_splice(&vm.threads, idx, 1);
    LxThreadCleanup(vm.curThread);
}

static void *runCallableInNewThread(void *arg) {
    acquireGVL();
    pthread_t tid = pthread_self();
    ObjClosure *closure = arg;
    ASSERT(closure);
    THREAD_DEBUG(2, "in new thread %lu", pthread_self());
    LxThread *th = FIND_NEW_THREAD();
    ASSERT(th);
    th->tid = tid;
    th->status = THREAD_RUNNING;
    vm.curThread = th;
    ASSERT(tid == pthread_self());
    ASSERT(vm.curThread->tid == pthread_self());
    push(OBJ_VAL(closure));
    if (vm.exited) {
        THREAD_DEBUG(2, "vm exited, quitting new thread %lu", pthread_self());
        releaseGVL();
        pthread_exit(NULL);
    }
    THREAD_DEBUG(2, "calling callable %lu", pthread_self());
    callCallable(OBJ_VAL(closure), 0, false, NULL);
    exitingThread();
    THREAD_DEBUG(2, "exiting thread %lu", pthread_self());
    releaseGVL();
    pthread_exit(NULL);
}


// usage: newThread(fun() { ... });
Value lxNewThread(int argCount, Value *args) {
    CHECK_ARITY("newThread", 1, 1, argCount);
    Value closure = *args;
    CHECK_ARG_BUILTIN_TYPE(closure, IS_CLOSURE_FUNC, "function", 1);
    ObjClosure *func = AS_CLOSURE(closure);
    pthread_t tnew;
    newThreadSetup(vm.curThread);
    releaseGVL();
    if (pthread_create(&tnew, NULL, runCallableInNewThread, func) == 0) {
        acquireGVL();
        THREAD_DEBUG(2, "created thread id %lu", (unsigned long)tnew);
        return NUMBER_VAL((unsigned long)tnew);
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
    Value tidNum = *args;
    CHECK_ARG_BUILTIN_TYPE(tidNum, IS_NUMBER_FUNC, "number", 1);
    double num = AS_NUMBER(tidNum);
    THREAD_DEBUG(2, "Joining thread id %lu\n", (unsigned long)num);
    int ret = 0;

    releaseGVL();
    // blocking call until given thread ends execution
    if ((ret = pthread_join((pthread_t)num, NULL)) != 0) {
        THREAD_DEBUG(1, "Error joining thread: (ret=%d), throwing", ret);
        acquireGVL();
        // TODO: throw lxThreadErrClass
        throwErrorFmt(lxErrClass, "Error joining thread");
    }
    acquireGVL();
    THREAD_DEBUG(2, "Joined thread id %lu\n", (unsigned long)num);
    return NIL_VAL;
}


Value lxThreadScheduleStatic(int argCount, Value *args) {
    releaseGVL();
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
    if (vm.inited) {
        tableSet(selfObj->hiddenFields, OBJ_VAL(INTERN("th")), OBJ_VAL(internalObj));
    } else {
        tableSet(selfObj->hiddenFields, NUMBER_VAL(1), OBJ_VAL(internalObj));
    }
    return self;
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
    Value internal = getHiddenProp(mutex, INTERN("mutex"));
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
    tableSet(selfObj->hiddenFields, OBJ_VAL(INTERN("mutex")), OBJ_VAL(internalObj));
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
    addNativeMethod(threadStatic, "schedule", lxThreadScheduleStatic);

    nativeThreadInit = addNativeMethod(threadClass, "init", lxThreadInit);

    ObjClass *mutexClass = addGlobalClass("Mutex", lxObjClass);
    lxMutexClass = mutexClass;
    addNativeMethod(mutexClass, "init", lxMutexInit);
    addNativeMethod(mutexClass, "lock", lxMutexLock);
    addNativeMethod(mutexClass, "unlock", lxMutexUnlock);
}
