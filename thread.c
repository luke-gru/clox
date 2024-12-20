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

void vmCheckInts(LxThread *th) {
    if (UNLIKELY(INTERRUPTED_ANY(th))) {
        threadExecuteInterrupts(th);
    }
}

static int threadGetInterrupt(LxThread *th) {
    if (th->interruptFlags & INTERRUPT_TRAP) {
        return INTERRUPT_TRAP;
    } else if (th->interruptFlags & INTERRUPT_GENERAL) {
        return INTERRUPT_GENERAL;
    } else {
        return INTERRUPT_NONE;
    }
}

const char *threadStatusName(ThreadStatus status) {
    switch (status) {
        case THREAD_STOPPED:
            return "STOPPED";
        case THREAD_SLEEPING:
            return "SLEEPING";
        case THREAD_READY:
            return "READY";
        case THREAD_RUNNING:
            return "RUNNING";
        case THREAD_KILLED:
            return "KILLED";
        case THREAD_ZOMBIE:
            return "ZOMBIE";
        default:
            return "UNKNOWN?";
    }
}

/*
 * Execute all interrupt signal handlers on the main thread.
 */
void threadExecuteInterrupts(LxThread *th) {
    ASSERT(vm.curThread == th);
    int interrupt = 0;
    while ((interrupt = threadGetInterrupt(th)) != INTERRUPT_NONE) {
        if (interrupt == INTERRUPT_TRAP && th == vm.mainThread) {
            th->interruptFlags &= (~interrupt);
            int sig;
            while ((sig = getSignal()) != -1) {
                if (execSignal(th, sig) != 0) {
                    break;
                }
            }
        } else if (interrupt == INTERRUPT_GENERAL) { // sent 'exit' interrupt to living thread
            THREAD_DEBUG(1, "Thread %lu got exit interrupt, exiting", th->tid);
            ASSERT(th != vm.mainThread);
            if (GVLOwner == th->tid) {
                THREAD_DEBUG(1, "thread releasing GVL before exit");
                releaseGVL(THREAD_ZOMBIE);
                THREAD_DEBUG(1, "thread released GVL before exit");
            } else {
                THREAD_DEBUG(1, "thread setting to zombie");
                th->status = THREAD_ZOMBIE;
                pthread_cond_signal(&vm.GVLCond); // signal waiters
            }
            THREAD_DEBUG(1, "thread exiting");
            vm.numLivingThreads--;
            th->status = THREAD_ZOMBIE;
            pthread_exit(NULL);
        }
    }
}

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
            continue;
        }
    }
    return true;
}

static void LxThreadSetup(LxThread *th) {
    th->tid = pthread_self();
    th->status = THREAD_STOPPED;
    th->ec = NULL;
    vec_init(&th->v_ecs);
    th->openUpvalues = NULL;
    th->thisObj = NULL;
    vec_init(&th->v_thisStack);
    vec_reserve(&th->v_thisStack, FRAMES_MAX);
    vec_init(&th->v_crefStack);
    vec_reserve(&th->v_crefStack, FRAMES_MAX);
    vec_init(&th->v_blockStack);
    vec_reserve(&th->v_blockStack, FRAMES_MAX);
    vec_init(&th->stackObjects);
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
    th->tlsMap = NULL;
    pthread_mutex_init(&th->sleepMutex, NULL);
    pthread_cond_init(&th->sleepCond, NULL);
    pthread_mutex_init(&th->interruptLock, NULL);
    th->interruptFlags = INTERRUPT_NONE;
    th->opsRemaining = THREAD_OPS_UNTIL_SWITCH;
    th->exitStatus = 0;
    th->joined = false;
    th->detached = false;
    th->lastOp = -1;
    vec_init(&th->lockedMutexes);
    vec_init(&th->recurseSet);
}

static void LxThreadCleanup(LxThread *th) {
    vec_deinit(&th->v_ecs);
    vec_deinit(&th->v_thisStack);
    vec_deinit(&th->v_crefStack);
    vec_deinit(&th->v_blockStack);
    vec_deinit(&th->stackObjects);
    vec_deinit(&th->lockedMutexes);
    vec_deinit(&th->recurseSet);
    pthread_mutex_destroy(&th->sleepMutex);
    pthread_cond_destroy(&th->sleepCond);
}

typedef struct NewThreadArgs {
    ObjClosure *func;
    LxThread *th;
} NewThreadArgs;

// NOTE: This thread is not running yet, this is just setting it up.
// It doesn't have a thread id (tid) or its own stack yet.
static ObjInstance *newThreadSetup(LxThread *parentThread) {
    ASSERT(parentThread);
    THREAD_DEBUG(3, "New thread setup");
    ObjInstance *thInstance = newInstance(lxThreadClass, NEWOBJ_FLAG_OLD|NEWOBJ_FLAG_HIDDEN);
    hideFromGC((Obj*)thInstance);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxThread), NULL, NULL, NEWOBJ_FLAG_NONE);
    thInstance->internal = internalObj;
    LxThread *th = ALLOCATE(LxThread, 1);
    LxThreadSetup(th);
    internalObj->data = th;

    // set thread state from current (last) thread
    VMExecContext *ctx = NULL; int ctxIdx = 0;
    vec_foreach(&parentThread->v_ecs, ctx, ctxIdx) {
        VMExecContext *newCtx = ALLOCATE(VMExecContext, 1);
        memcpy(newCtx, ctx, sizeof(VMExecContext));
        newCtx->stack = ALLOCATE(Value, ctx->stack_capa);
        newCtx->stack_capa = ctx->stack_capa;
        memcpy(newCtx->stack, ctx->stack, sizeof(Value)*newCtx->stack_capa);
        newCtx->frames = ALLOCATE(CallFrame, ctx->frames_capa);
        newCtx->frames_capa = ctx->frames_capa;
        memcpy(newCtx->frames, ctx->frames, sizeof(CallFrame)*ctx->frames_capa);
        newCtx->stackTop = newCtx->stack + (ctx->stackTop-ctx->stack);
        newCtx->stackTop--; // for the two current stack objects that newThread() creates
        newCtx->stackTop--;
        newCtx->frameCount = 1;
        newCtx->lastValue = NULL;
        vec_push(&th->v_ecs, newCtx);
    }
    th->ec = vec_last(&th->v_ecs);
    th->thisObj = NULL;
    th->lastValue = NULL;
    th->errInfo = NULL;
    th->inCCall = 0;
    th->cCallJumpBufSet = false;
    th->cCallThrew = false;
    th->returnedFromNativeErr = false;
    memset(&th->cCallJumpBuf, 0, sizeof(jmp_buf));
    th->vmRunLvl = 0;
    th->lastSplatNumArgs = -1;

    th->status = THREAD_READY;
    th->tid = 0; // unknown, not yet created
    vec_push(&vm.threads, thInstance);
    unhideFromGC((Obj*)thInstance);
    THREAD_DEBUG(3, "New thread setup done");
    return thInstance;
}

void exitingThread(LxThread *th) {
    ASSERT(th);
    th->status = THREAD_ZOMBIE;
    th->openUpvalues = NULL;
    LxThreadCleanup(th);
}

static void *runCallableInNewThread(void *arg) {
    NewThreadArgs *tArgs = (NewThreadArgs*)arg;
    pthread_t tid = pthread_self();
    LxThread *th = tArgs->th;
    ASSERT(th->status == THREAD_READY);
    th->tid = tid;
    th->pid = getpid();
    ASSERT(th->tid > 0);
    THREAD_DEBUG(2, "switching to newly created thread, acquiring lock %lu", th->tid);
    THREAD_DEBUG(2, "acquiring GVL");
    vm.numLivingThreads++;
    acquireGVL();
    THREAD_DEBUG(2, "acquired GVL");
    th = vm.curThread;
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
    if (vm.exiting || vm.exited) {
        THREAD_DEBUG(2, "vm exited, quitting new thread %lu", pthread_self());
        releaseGVL(THREAD_ZOMBIE);
        vm.numLivingThreads--;
        pthread_exit(NULL);
    }
    THREAD_DEBUG(2, "calling callable %lu", pthread_self());
    th->status = THREAD_RUNNING;
    callCallable(OBJ_VAL(closure), 0, false, NULL);
    THREAD_DEBUG(2, "Exiting thread (returned) %lu", pthread_self());
    stopVM(0); // actually just exits the thread if it's not the main thread
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
    releaseGVL(THREAD_STOPPED);
    if (pthread_create(&tnew, NULL, runCallableInNewThread, thArgs) == 0) {
        acquireGVL();
        // it's important the new thread has a tid even if it hasn't started running yet
        if (th->tid == 0) {
            th->tid = tnew;
        }
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
    ASSERT(th);
    ASSERT(th->tid != 0);
    THREAD_DEBUG(2, "Joining thread id %lu\n", th->tid);
    int ret = 0;

    pthread_t tid = th->tid;
    releaseGVL(THREAD_STOPPED);
    // blocking call until given thread ends execution
    if ((ret = pthread_join(th->tid, NULL)) != 0) {
        THREAD_DEBUG(1, "Error joining thread: (ret=%d), throwing", ret);
        acquireGVL();
        // TODO: throw lxThreadErrClass
        throwErrorFmt(lxErrClass, "Error joining thread");
    }
    THREAD_DEBUG(2, "Joined thread id %lu, acquiring GVL\n", tid);
    acquireGVL();
    THREAD_DEBUG(2, "Joined thread id %lu\n", tid);
    th = FIND_THREAD(tid);
    if (th) {
        th->joined = true;
    }
    return NIL_VAL;
}

static Value lxThreadMainStatic(int argCount, Value *args) {
    CHECK_ARITY("Thread.main", 1, 1, argCount);
    return OBJ_VAL(vec_first(&vm.threads));
}

static Value lxThreadCurrentStatic(int argCount, Value *args) {
    CHECK_ARITY("Thread.current", 1, 1, argCount);
    ObjInstance *curThread = FIND_THREAD_INSTANCE(vm.curThread->tid);
    return OBJ_VAL(curThread);
}

static Value lxThreadScheduleStatic(int argCount, Value *args) {
    CHECK_ARITY("Thread.schedule", 1, 1, argCount);
    LxThread *oldTh = vm.curThread;
    (void)oldTh;
    releaseGVL(THREAD_STOPPED);
// not available on OSX
#ifdef __linux__
    sched_yield();
#else
    threadSleepNano(oldTh, 100);
#endif
    acquireGVL();
    return NIL_VAL;
}

Value lxThreadInit(int argCount, Value *args) {
    CHECK_ARITY("Thread#init", 1, 1, argCount);
    callSuper(0, NULL, NULL);
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxThread), NULL, NULL, NEWOBJ_FLAG_NONE);
    LxThread *th = ALLOCATE(LxThread, 1);
    LxThreadSetup(th);
    internalObj->data = th;
    selfObj->internal = internalObj;
    return self;
}

void threadSchedule(LxThread *th) {
    ASSERT(th != vm.curThread);
    LxThread *oldTh = vm.curThread;
    (void)oldTh;
    releaseGVL(THREAD_STOPPED);
    pthread_cond_signal(&th->sleepCond);
#ifdef __linux__
    sched_yield();
#else
    threadSleepNano(oldTh, 100);
#endif
    acquireGVL();
}

// called by Process.signal
void threadCheckSignals(LxThread *main) {
    threadInterrupt(main, true);
}

void threadInterrupt(LxThread *th, bool isTrap) {
    if (isTrap) {
        ASSERT(th == vm.mainThread);
    }
    pthread_mutex_lock(&th->interruptLock);
    if (isTrap) {
        SET_TRAP_INTERRUPT(th);
    } else {
        SET_INTERRUPT(th);
    }
    pthread_mutex_unlock(&th->interruptLock);
    if (vm.curThread != th) {
        threadSchedule(th);
    } else {
        VM_CHECK_INTS(th);
    }
}

static Value lxThreadThrow(int argCount, Value *args) {
    CHECK_ARITY("Thread#throw", 2, 2, argCount);
    Value self = *args;
    Value err = args[1];
    CHECK_ARG_IS_A(err, lxErrClass, 1);
    LxThread *th = THREAD_GETHIDDEN(self);
    Value ret = NIL_VAL;
    if (th->status == THREAD_SLEEPING || th->status == THREAD_STOPPED || th->status == THREAD_RUNNING) {
        if (IS_NIL(th->lastErrorThrown)) {
            th->errorToThrow = err;
        }
        threadSchedule(th);
    }
    return ret;
}

void threadDetach(LxThread *th) {
    ASSERT(th && th != vm.curThread);
    ASSERT(th->tid > 0);
    th->detached = true;
    pthread_detach(th->tid);
    vm.numDetachedThreads++;
}

static Value lxThreadDetach(int argCount, Value *args) {
    CHECK_ARITY("Thread#detach", 1, 1, argCount);
    Value self = *args;
    LxThread *th = THREAD_GETHIDDEN(self);
    if (th == vm.mainThread) {
        return BOOL_VAL(false); // throw?
    }
    if (th->detached || th->status == THREAD_KILLED || th->status == THREAD_ZOMBIE) {
        return BOOL_VAL(false);
    }
    threadDetach(th);
    return BOOL_VAL(true);
}

static Value lxThreadGetTLS(int argCount, Value *args) {
    CHECK_ARITY("Thread#opIndexGet", 2, 2, argCount);
    Value self = *args;
    Value key = args[1];
    LxThread *th = THREAD_GETHIDDEN(self);
    if (!th->tlsMap) {
        th->tlsMap = AS_MAP(newMap());
    }
    Value val;
    if (mapGet(OBJ_VAL(th->tlsMap), key, &val)) {
        return val;
    } else {
        return NIL_VAL;
    }
}

static Value lxThreadSetTLS(int argCount, Value *args) {
    CHECK_ARITY("Thread#opIndexSet", 3, 3, argCount);
    Value self = *args;
    Value key = args[1];
    Value val = args[2];
    LxThread *th = THREAD_GETHIDDEN(self);
    if (!th->tlsMap) {
        th->tlsMap = AS_MAP(newMap());
    }
    mapSet(OBJ_VAL(th->tlsMap), key, val);
    return val;
}

typedef struct LxMutex {
    LxThread *owner;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int waiting;
} LxMutex;

static void setupMutex(LxMutex *mutex) {
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
    vec_push(&th->lockedMutexes, mutex);
    pthread_mutex_unlock(&mutex->lock); // can block
}

static void lockMutex(LxMutex *mutex) {
    pthread_mutex_lock(&mutex->lock); // can block
    LxThread *th = vm.curThread;
    if (mutex->owner == NULL) {
        THREAD_DEBUG(1, "Thread %lu LOCKED mutex (no contention)", th->tid);
        ASSERT(mutex->waiting == 0);
        mutex->owner = th;
        th->mutexCounter++;
        vec_push(&th->lockedMutexes, mutex);
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

void forceUnlockMutexes(LxThread *th) {
    if (th->mutexCounter == 0) return;
    LxMutex *m = NULL;
    while (th->lockedMutexes.length > 0 && (m = (LxMutex*)vec_first(&th->lockedMutexes))) {
        threadForceUnlockMutex(th, m);
    }
}

void threadForceUnlockMutex(LxThread *th, LxMutex *mutex) {
    pthread_mutex_lock(&mutex->lock);
    ASSERT(mutex->owner == th); // TODO: throw error
    mutex->owner = NULL;
    vec_remove(&th->lockedMutexes, mutex);
    THREAD_DEBUG(1, "Thread %lu unlocking mutex (forceful)...", th->tid);
    if (mutex->waiting > 0) {
        THREAD_DEBUG(1, "Thread %lu signaling waiter(s)...", th->tid);
        pthread_cond_signal(&mutex->cond);
    }
    th->mutexCounter--;
    THREAD_DEBUG(1, "Thread %lu UNLOCKED mutex (forceful)", th->tid);
    pthread_mutex_unlock(&mutex->lock);
}

static void unlockMutex(LxMutex *mutex) {
    pthread_mutex_lock(&mutex->lock);
    LxThread *th = vm.curThread;
    ASSERT(mutex->owner == th); // TODO: throw error
    mutex->owner = NULL;
    vec_remove(&th->lockedMutexes, mutex);
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
    ASSERT(m);
    return m;
}

static Value lxMutexInit(int argCount, Value *args) {
    CHECK_ARITY("Mutex#init", 1, 1, argCount);
    callSuper(0, NULL, NULL);
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxMutex), NULL, NULL, NEWOBJ_FLAG_NONE);
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

struct exec_recursive_params {
    stopRecursionFn func;
    Value obj;
    Value arg;
    vec_void_t *seenSet;
};

static vec_void_t *getRecursiveList(void) {
    return &vm.curThread->recurseSet;
}

static void addSeenSet(vec_void_t *seenSet, Obj *obj) {
    int found = -1;
    vec_find(seenSet, obj, found);
    if (found == -1) {
        vec_push(seenSet, obj);
    }
}

static bool hasSeen(vec_void_t *seenSet, Obj *obj) {
    int found = -1;
    vec_find(seenSet, obj, found);
    return found != -1;
}

static void removeSeenSet(vec_void_t *seenSet, Obj *obj) {
    vec_remove(seenSet, obj);
}

static void *execStopRecursionIter(void *data) {
    struct exec_recursive_params *p = (void *)data;
    return (void*)(*p->func)(p->obj, p->arg, 0);
}

Value execStopRecursion(stopRecursionFn fn, Value obj, Value arg) {
    volatile struct exec_recursive_params p;
    p.func = fn;
    p.obj = obj;
    p.arg = arg;
    p.seenSet = getRecursiveList();

    if (IS_OBJ(p.obj)) {
        if (hasSeen(p.seenSet, AS_OBJ(p.obj))) {
            Value recurseErr = newError(lxRecursionErrClass, NIL_VAL);
            throwError(recurseErr);
        }
        addSeenSet(p.seenSet, AS_OBJ(p.obj));
    }
    ErrTag status = TAG_NONE;
    Value result = (Value)vm_protect(execStopRecursionIter, (void*)&p, NULL, &status);
    if (IS_OBJ(p.obj)) {
        removeSeenSet(p.seenSet, AS_OBJ(p.obj));
    }
    if (status != TAG_NONE) {
        popErrInfo();
        return result;
    } else {
        return result;
    }
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
    addNativeMethod(threadClass, "opIndexGet", lxThreadGetTLS);
    addNativeMethod(threadClass, "opIndexSet", lxThreadSetTLS);

    ObjClass *mutexClass = addGlobalClass("Mutex", lxObjClass);
    lxMutexClass = mutexClass;
    addNativeMethod(mutexClass, "init", lxMutexInit);
    addNativeMethod(mutexClass, "lock", lxMutexLock);
    addNativeMethod(mutexClass, "unlock", lxMutexUnlock);
}
