#include <time.h>
#include <sys/types.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"

typedef struct LxTime {
    time_t sinceEpoch;
    struct tm tm;
    bool tmGot;
} LxTime;

typedef struct LxTimer {
    struct timespec tp;
    clockid_t clock;
} LxTimer;

static struct timespec timeAdd(struct timespec a, struct timespec b) {
    if (a.tv_nsec + b.tv_nsec >= 1E9) {
        struct timespec ret = {
            .tv_sec = a.tv_sec + b.tv_sec + 1,
            .tv_nsec = a.tv_nsec + b.tv_nsec - 1E9
        };
        return ret;
    } else {
        struct timespec ret = {
            .tv_sec = a.tv_sec + b.tv_sec,
            .tv_nsec = a.tv_nsec + b.tv_nsec
        };
        return ret;
    }
}

static struct timespec timeDiff(struct timespec t1 /* old */,
        struct timespec t2 /* new */) {
    if (t2.tv_nsec < t1.tv_nsec) {
        struct timespec ret = {
            .tv_sec = t2.tv_sec - 1 - t1.tv_sec,
            .tv_nsec = 1E9 + t2.tv_nsec - t1.tv_nsec
        };
        return ret;
    } else {
        struct timespec ret = {
            .tv_sec = t2.tv_sec - t1.tv_sec,
            .tv_nsec = t2.tv_nsec - t1.tv_nsec
        };
        return ret;
    }
}

// Is `t1` older than (or the same as) `t2`?
static bool isTimeOlder(struct timespec t1, struct timespec t2) {
    if (t1.tv_sec < t2.tv_sec) return true;
    if (t1.tv_nsec <= t2.tv_nsec) return true;
    return false;
}

static double timeSeconds(struct timespec time) {
    return time.tv_sec + time.tv_nsec/1E9;
}

ObjClass *lxTimeClass;
ObjClass *lxTimerClass;

static inline LxTime *timeGetHidden(Value timeVal) {
    ObjInternal *internal = AS_INSTANCE(timeVal)->internal;
    return (LxTime*)internal->data;
}

static inline LxTimer *timerGethidden(Value timerVal) {
    ObjInternal *internal = AS_INSTANCE(timerVal)->internal;
    return (LxTimer*)internal->data;
}

static Value lxTimeInit(int argCount, Value *args) {
    CHECK_ARITY("Time#init", 1, 1, argCount);
    callSuper(0, NULL, NULL);
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, 0, NULL, NULL, NEWOBJ_FLAG_NONE);
    LxTime *t = ALLOCATE(LxTime, 1);
    time(&t->sinceEpoch);
    t->tmGot = false;
    internalObj->data = t;
    internalObj->dataSz = sizeof(LxTime);
    selfObj->internal = internalObj;
    return self;
}

static Value lxTimeToString(int argCount, Value *args) {
    LxTime *time = timeGetHidden(*args);
    char buf[100];
    memset(buf, 0, 100);
    char *res = ctime_r(&time->sinceEpoch, buf);
    if (!res) {
        return OBJ_VAL(copyString("", 0, NEWOBJ_FLAG_NONE));
    }
    return OBJ_VAL(copyString(buf, strlen(buf), NEWOBJ_FLAG_NONE));
}

static Value lxTimerInitEmpty(int argCount, Value *args) {
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, 0, NULL, NULL, NEWOBJ_FLAG_NONE);
    LxTimer *t = ALLOCATE(LxTimer, 1);
    internalObj->data = t;
    internalObj->dataSz = sizeof(LxTime);
    selfObj->internal = internalObj;
    return self;
}

static Value emptyTimer(void) {
    ObjInstance *inst = newInstance(lxTimerClass, NEWOBJ_FLAG_NONE);
    Value ret = OBJ_VAL(inst);
    lxTimerInitEmpty(1, &ret);
    return ret;
}

static Value lxTimerInit(int argCount, Value *args) {
    CHECK_ARITY("Timer#init", 1, 2, argCount);
    callSuper(0, NULL, NULL);
    clock_t clock = CLOCK_MONOTONIC; // default
    if (argCount == 2) {
        CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
        double givenClockD = AS_NUMBER(args[1]);
        clock = (clock_t)givenClockD;
    }
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, 0, NULL, NULL, NEWOBJ_FLAG_NONE);
    LxTimer *t = ALLOCATE(LxTimer, 1);
    t->clock = clock;
    int res = clock_gettime(t->clock, &t->tp);
    if (res == -1) {
        throwErrorFmt(lxErrClass, "Could not get timer time");
    }
    internalObj->data = t;
    internalObj->dataSz = sizeof(LxTime);
    selfObj->internal = internalObj;
    return self;
}


static Value lxTimerOpAdd(int argCount, Value *args) {
    CHECK_ARITY("Timer#opAdd", 2, 2, argCount);
    Value self = *args;
    Value other = args[1];
    CHECK_ARG_IS_A(other, lxTimerClass, 1);
    Value ret = emptyTimer();
    LxTimer *a = timerGethidden(self);
    LxTimer *b = timerGethidden(other);
    LxTimer *tnew = timerGethidden(ret);
    struct timespec newTp = timeAdd(a->tp, b->tp);
    tnew->tp = newTp;
    tnew->clock = a->clock;
    return ret;
}

static Value lxTimerOpDiff(int argCount, Value *args) {
    CHECK_ARITY("Timer#opAdd", 2, 2, argCount);
    Value self = *args;
    Value other = args[1];
    CHECK_ARG_IS_A(other, lxTimerClass, 1);
    Value ret = emptyTimer();
    LxTimer *newer = timerGethidden(self);
    LxTimer *older = timerGethidden(other);
    if (!isTimeOlder(older->tp, newer->tp)) {
        throwErrorFmt(lxArgErrClass, "Given time is newer than `self`");
    }
    LxTimer *tnew = timerGethidden(ret);
    // old timer is `self` (t2 - t1) is t2.opDiff(t1)
    struct timespec newTp = timeDiff(older->tp, newer->tp);
    tnew->tp = newTp;
    tnew->clock = newer->clock;
    return ret;
}

static Value lxTimerToString(int argCount, Value *args) {
    CHECK_ARITY("Timer#toString", 1, 1, argCount);
    Value self = *args;
    LxTimer *t = timerGethidden(self);
    return OBJ_VAL(valueToString(NUMBER_VAL(timeSeconds(t->tp)), copyString, NEWOBJ_FLAG_NONE));
}

static Value lxTimerSeconds(int argCount, Value *args) {
    CHECK_ARITY("Timer#seconds", 1, 1, argCount);
    Value self = *args;
    LxTimer *t = timerGethidden(self);
    return NUMBER_VAL(timeSeconds(t->tp));
}

void Init_TimeClass() {
    ObjClass *timeClass = addGlobalClass("Time", lxObjClass);
    lxTimeClass = timeClass;

    addNativeMethod(timeClass, "init", lxTimeInit);
    addNativeMethod(timeClass, "toString", lxTimeToString);

    ObjClass *timerClass = addGlobalClass("Timer", lxObjClass);
    addNativeMethod(timerClass, "init", lxTimerInit);
    addNativeMethod(timerClass, "opAdd", lxTimerOpAdd);
    addNativeMethod(timerClass, "opDiff", lxTimerOpDiff);
    addNativeMethod(timerClass, "seconds", lxTimerSeconds);
    addNativeMethod(timerClass, "toString", lxTimerToString);
    Value timerClassVal = OBJ_VAL(timerClass);
    addConstantUnder("CLOCK_REALTIME", NUMBER_VAL(CLOCK_REALTIME), timerClassVal);
    addConstantUnder("CLOCK_MONOTONIC", NUMBER_VAL(CLOCK_MONOTONIC), timerClassVal);
    addConstantUnder("CLOCK_PROCESS_CPUTIME_ID", NUMBER_VAL(CLOCK_PROCESS_CPUTIME_ID), timerClassVal);
    addConstantUnder("CLOCK_THREAD_CPUTIME_ID", NUMBER_VAL(CLOCK_THREAD_CPUTIME_ID), timerClassVal);
    lxTimerClass = timerClass;
}
