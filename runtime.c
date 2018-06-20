#include <time.h>
#include <stdio.h>
#include "runtime.h"
#include "object.h"
#include "memory.h"
#include "debug.h"

Value runtimeNativeClock(int argCount, Value *args) {
    CHECK_ARGS("clock", 0, 0, argCount);
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

Value runtimeNativeTypeof(int argCount, Value *args) {
    CHECK_ARGS("typeof", 1, 1, argCount);
    const char *strType = typeOfVal(*args);
    return OBJ_VAL(copyString(strType, strlen(strType)));
}

static void markInternalAry(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    ASSERT(internal);
    ValueArray *valAry = internal->data;
    ASSERT(valAry);
    for (int i = 0; i < valAry->count; i++) {
        if (!IS_OBJ(valAry->values[i])) continue;
        blackenObject(AS_OBJ(valAry->values[i]));
    }
}

static void freeInternalAry(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    ValueArray *valAry = internal->data;
    ASSERT(internal);
    ASSERT(valAry);
    freeValueArray(valAry);
    FREE(ValueArray, valAry); // release the actual memory
}

// var a = Array();
Value lxArrayInit(int argCount, Value *args) {
    CHECK_ARGS("Array#init", 1, -1, argCount);
    Value self = *args;
    ASSERT(IS_ARRAY(self));
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(NULL, markInternalAry, freeInternalAry);
    hideFromGC((Obj*)internalObj);
    ValueArray *ary = ALLOCATE(ValueArray, 1);
    initValueArray(ary);
    internalObj->data = ary;
    tableSet(&selfObj->hiddenFields, copyString("ary", 3), OBJ_VAL(internalObj));
    for (int i = 1; i < argCount; i++) {
        writeValueArray(ary, args[i]);
    }
    unhideFromGC((Obj*)internalObj);
    return self;
}

// a.push(1);
Value lxArrayPush(int argCount, Value *args) {
    CHECK_ARGS("Array#push", 2, 2, argCount);
    Value self = *args;
    ASSERT(IS_ARRAY(self));
    ObjInstance *selfObj = AS_INSTANCE(self);
    Value internalObjVal;
    ASSERT(tableGet(&selfObj->hiddenFields, copyString("ary", 3), &internalObjVal));
    ValueArray *ary = (ValueArray*)internalGetData(AS_INTERNAL(internalObjVal));
    ASSERT(ary);
    writeValueArray(ary, args[1]);
    return self;
}

// print a;
// OR
// a.toString();
Value lxArrayToString(int argCount, Value *args) {
    CHECK_ARGS("Array#toString", 1, 1, argCount);
    Value self = *args;
    ASSERT(IS_ARRAY(self));
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjString *ret = newString("[", 1);
    hideFromGC((Obj*)ret);
    Value internalObjVal;
    ASSERT(tableGet(&selfObj->hiddenFields, copyString("ary", 3), &internalObjVal));
    ValueArray *ary = (ValueArray*)internalGetData(AS_INTERNAL(internalObjVal));
    ASSERT(ary);
    for (int i = 0; i < ary->count; i++) {
        Value val = ary->values[i];
        ObjString *buf = valueToString(val);
        pushCString(ret, buf->chars, strlen(buf->chars));
        unhideFromGC((Obj*)buf);
        freeObject((Obj*)buf);
        if (i < (ary->count-1)) {
            pushCString(ret, ",", 1);
        }
    }
    pushCString(ret, "]", 1);
    unhideFromGC((Obj*)ret);
    return OBJ_VAL(ret);
}

// TODO
#define CHECK_ARG_TYPE(...)

Value lxArrayIndexGet(int argCount, Value *args) {
    CHECK_ARGS("Array#[]", 2, 2, argCount);
    Value self = args[0];
    ASSERT(IS_ARRAY(self));
    ObjInstance *selfObj = AS_INSTANCE(self);
    Value num = args[1];
    CHECK_ARG_TYPE(num, VAL_T_NUMBER, 1);
    Value internalObjVal;
    ASSERT(tableGet(&selfObj->hiddenFields, copyString("ary", 3), &internalObjVal));
    ValueArray *ary = (ValueArray*)internalGetData(AS_INTERNAL(internalObjVal));
    ASSERT(ary);
    int idx = (int)AS_NUMBER(num);
    if (idx < 0) {
        // FIXME: throw error
        return NIL_VAL;
    }

    if (idx < ary->count) {
        return ary->values[idx];
    } else {
        return NIL_VAL;
    }
}

Value lxArrayIndexSet(int argCount, Value *args) {
    CHECK_ARGS("Array#[]=", 3, 3, argCount);
    Value self = args[0];
    ASSERT(IS_ARRAY(self));
    ObjInstance *selfObj = AS_INSTANCE(self);
    Value num = args[1];
    Value rval = args[2];
    CHECK_ARG_TYPE(num, VAL_T_NUMBER, 1);
    Value internalObjVal;
    ASSERT(tableGet(&selfObj->hiddenFields, copyString("ary", 3), &internalObjVal));
    ValueArray *ary = (ValueArray*)internalGetData(AS_INTERNAL(internalObjVal));
    ASSERT(ary);
    int idx = (int)AS_NUMBER(num);
    if (idx < 0) {
        // FIXME: throw error
        return NIL_VAL;
    }

    if (idx < ary->count) {
        ary->values[idx] = rval;
    } else {
        // TODO: throw error or grow array?
        return NIL_VAL;
    }
    return rval;
}

bool runtimeCheckArgs(int min, int max, int actual) {
    return min <= actual && (max >= actual || max == -1);
}

