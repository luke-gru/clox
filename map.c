#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"

ObjClass *lxMapClass;

static void markInternalMap(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    Table *map = (Table*)internal->data;
    ASSERT(map);
    blackenTable(map);
}

static void freeInternalMap(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    Table *map = (Table*)internal->data;
    ASSERT(map);
    freeTable(map);
    FREE(Table, map);
}

Value lxMapInit(int argCount, Value *args) {
    // TODO: call super?
    CHECK_ARITY("Map#init", 1, -1, argCount);
    Value self = args[0];
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalMap = newInternalObject(
        NULL, sizeof(Table), markInternalMap, freeInternalMap
    );
    Table *map = ALLOCATE(Table, 1);
    initTable(map);
    internalMap->data = map;
    tableSet(&selfObj->hiddenFields, OBJ_VAL(
        internedString("map", 3)), OBJ_VAL(internalMap));

    if (argCount == 1) {
        return self;
    }

    if (argCount == 2) {
        Value ary = args[1];
        CHECK_ARG_IS_INSTANCE_OF(ary, lxAryClass, 1);
        ValueArray *aryInt = ARRAY_GETHIDDEN(ary);
        for (int i = 0; i < aryInt->count; i++) {
            Value el = aryInt->values[i];
            if (!IS_AN_ARRAY(el)) {
                throwErrorFmt(lxTypeErrClass, "Expected array element to be an array of length 2, got a: %s", typeOfVal(el));
            }
            if (ARRAY_SIZE(el) != 2) {
                throwArgErrorFmt("Wrong array size given, expected 2, got: %d",
                        ARRAY_SIZE(el));
            }
            Value mapKey = ARRAY_GET(el, 0);
            Value mapVal = ARRAY_GET(el, 1);
            tableSet(map, mapKey, mapVal);
        }
    } else {
        throwArgErrorFmt("Expected 1 argument, got %d", argCount-1);
    }
    return self;
}

static Value lxMapToString(int argCount, Value *args) {
    CHECK_ARITY("Map#toString", 1, 1, argCount);
    Value self = args[0];
    Obj *selfObj = AS_OBJ(self);
    Value ret = newStringInstance(copyString("{", 1));
    ObjString *bufRet = STRING_GETHIDDEN(ret);
    Table *map = MAP_GETHIDDEN(self);
    Entry e; int idx = 0;
    int sz = map->count; int i = 0;
    TABLE_FOREACH(map, e, idx) {
        if (IS_OBJ(e.key) && AS_OBJ(e.key) == selfObj) {
            pushCString(bufRet, "{...}", 5);
        } else {
            ObjString *keyS = valueToString(e.key, copyString);
            pushCString(bufRet, keyS->chars, strlen(keyS->chars));
        }
        pushCString(bufRet, " => ", 4);
        if (IS_OBJ(e.value) && AS_OBJ(e.value) == selfObj) {
            pushCString(bufRet, "{...}", 5);
        } else {
            ObjString *valS = valueToString(e.value, copyString);
            pushCString(bufRet, valS->chars, strlen(valS->chars));
        }

        if (i < (sz-1)) {
            pushCString(bufRet, ", ", 2);
        }
        i++;
    }

    pushCString(bufRet, "}", 1);

    return ret;
}

static Value lxMapOpIndexGet(int argCount, Value *args) {
    CHECK_ARITY("Map#[]", 2, 2, argCount);
    Value self = args[0];
    Table *map = MAP_GETHIDDEN(self);
    Value key = args[1];
    Value found;
    if (tableGet(map, key, &found)) {
        return found;
    } else {
        return NIL_VAL;
    }
}

static Value lxMapOpIndexSet(int argCount, Value *args) {
    CHECK_ARITY("Map#[]=", 3, 3, argCount);
    Value self = args[0];
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Map is frozen, cannot modify");
    }
    Table *map = MAP_GETHIDDEN(self);
    Value key = args[1];
    Value val = args[2];
    tableSet(map, key, val);
    return val;
}

static Value lxMapKeys(int argCount, Value *args) {
    CHECK_ARITY("Map#keys", 1, 1, argCount);
    Value self = args[0];
    Table *map = MAP_GETHIDDEN(self);
    Entry entry; int i = 0;
    Value ary = newArray();
    TABLE_FOREACH(map, entry, i) {
        arrayPush(ary, entry.key);
    }
    return ary;
}

static Value lxMapValues(int argCount, Value *args) {
    CHECK_ARITY("Map#values", 1, 1, argCount);
    Value self = args[0];
    Table *map = MAP_GETHIDDEN(self);
    Entry entry; int i = 0;
    Value ary = newArray();
    TABLE_FOREACH(map, entry, i) {
        arrayPush(ary, entry.value);
    }
    return ary;
}

static Value lxMapIter(int argCount, Value *args) {
    CHECK_ARITY("Map#iter", 1, 1, argCount);
    return createIterator(*args);
}

static Value lxMapOpEquals(int argCount, Value *args) {
    CHECK_ARITY("Map#==", 2, 2, argCount);
    return BOOL_VAL(mapEquals(args[0], args[1]));
}

static Value lxMapClear(int argCount, Value *args) {
    CHECK_ARITY("Map#clear", 1, 1, argCount);
    Value self = args[0];
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Map is frozen, cannot modify");
    }
    mapClear(self);
    return self;
}

void Init_MapClass() {
    // class Map
    ObjClass *mapClass = addGlobalClass("Map", lxObjClass);
    lxMapClass = mapClass;

    addNativeMethod(mapClass, "init", lxMapInit);
    addNativeMethod(mapClass, "opIndexGet", lxMapOpIndexGet);
    addNativeMethod(mapClass, "opIndexSet", lxMapOpIndexSet);
    addNativeMethod(mapClass, "opEquals", lxMapOpEquals);
    addNativeMethod(mapClass, "keys", lxMapKeys);
    addNativeMethod(mapClass, "values", lxMapValues);
    addNativeMethod(mapClass, "toString", lxMapToString);
    addNativeMethod(mapClass, "iter", lxMapIter);
    addNativeMethod(mapClass, "clear", lxMapClear);
}
