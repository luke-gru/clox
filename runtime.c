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
    tableSet(&selfObj->hiddenFields, OBJ_VAL(copyString("ary", 3)), OBJ_VAL(internalObj));
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
    ASSERT(tableGet(&selfObj->hiddenFields, OBJ_VAL(copyString("ary", 3)), &internalObjVal));
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
    Obj* selfObj = AS_OBJ(self);
    ObjString *ret = newString("[", 1);
    hideFromGC((Obj*)ret);
    ValueArray *ary = ARRAY_GETHIDDEN(self);
    for (int i = 0; i < ary->count; i++) {
        Value elVal = ary->values[i];
        if (IS_OBJ(elVal) && (AS_OBJ(elVal) == (Obj*)selfObj)) {
            pushCString(ret, "[...]", 5);
            continue;
        }
        ObjString *buf = valueToString(elVal);
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
    ASSERT(tableGet(&selfObj->hiddenFields, OBJ_VAL(copyString("ary", 3)), &internalObjVal));
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
    ASSERT(tableGet(&selfObj->hiddenFields, OBJ_VAL(copyString("ary", 3)), &internalObjVal));
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

// TODO
#define CHECK_ARG_OBJ_TYPE(...)

static void markInternalMap(Obj *internalObj) {
    // TODO
}

static void freeInternalMap(Obj *internalObj) {
    // TODO
}

Value lxMapInit(int argCount, Value *args) {
    CHECK_ARGS("Map#init", 1, -1, argCount);
    Value self = args[0];
    ASSERT(IS_MAP(self));
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalMap = newInternalObject(
        NULL, markInternalMap, freeInternalMap
    );
    hideFromGC((Obj*)internalMap);
    Table *map = ALLOCATE(Table, 1);
    initTable(map);
    internalMap->data = map;
    tableSet(&selfObj->hiddenFields, OBJ_VAL(
        copyString("map", 3)), OBJ_VAL(internalMap));
    unhideFromGC((Obj*)internalMap);

    if (argCount == 1) {
        return self;
    }

    if (argCount == 2) {
        Value ary = args[1];
        CHECK_ARG_OBJ_TYPE(ary, lxAryClass, 1);
        ObjInstance *aryInst = AS_INSTANCE(ary);
        Value internalAry;
        ASSERT(tableGet(&aryInst->hiddenFields,
            OBJ_VAL(copyString("ary", 3)),
            &internalAry));
        ValueArray *aryInt = (ValueArray*)internalGetData(AS_INTERNAL(internalAry));
        for (int i = 0; i < aryInt->count; i++) {
            Value el = aryInt->values[i];
            ASSERT(IS_ARRAY(el));
            if (ARRAY_SIZE(el) != 2) {
                fprintf(stderr, "Wrong array size given, expected 2");
                // TODO: throw error
                ASSERT(0);
            }
            Value mapKey = ARRAY_GET(el, 0);
            Value mapVal = ARRAY_GET(el, 1);
            ASSERT(tableSet(map, mapKey, mapVal));
        }
    } else {
        ASSERT(0);
    }
    return self;
}

bool runtimeCheckArgs(int min, int max, int actual) {
    return min <= actual && (max >= actual || max == -1);
}

