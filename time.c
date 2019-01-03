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

ObjClass *lxTimeClass;

LxTime *timeGetHidden(Value timeVal) {
    ObjInternal *internal = AS_INTERNAL(getHiddenProp(timeVal, INTERN("time")));
    return (LxTime*)internal->data;
}

static Value lxTimeInit(int argCount, Value *args) {
    CHECK_ARITY("Time#init", 1, 1, argCount);
    callSuper(0, NULL, NULL);
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, 0, NULL, NULL);
    LxTime *t = ALLOCATE(LxTime, 1);
    time(&t->sinceEpoch);
    t->tmGot = false;
    internalObj->data = t;
    internalObj->dataSz = sizeof(LxTime);
    selfObj->internal = internalObj;
    tableSet(selfObj->hiddenFields, OBJ_VAL(INTERN("time")), OBJ_VAL(internalObj));
    return self;
}

static Value lxTimeToString(int argCount, Value *args) {
    LxTime *time = timeGetHidden(*args);
    char buf[100];
    memset(buf, 0, 100);
    char *res = ctime_r(&time->sinceEpoch, buf);
    if (!res) {
        return newStringInstance(copyString("", 0));
    }
    return newStringInstance(copyString(buf, strlen(buf)));
}

void Init_TimeClass() {
    ObjClass *timeClass = addGlobalClass("Time", lxObjClass);
    lxTimeClass = timeClass;

    addNativeMethod(timeClass, "init", lxTimeInit);
    addNativeMethod(timeClass, "toString", lxTimeToString);
}
