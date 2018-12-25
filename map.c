#include <unistd.h>
#include <errno.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"

ObjClass *lxMapClass;

extern ObjNative *nativeMapInit;

ObjClass *lxEnvClass;
ObjInstance *lxEnv;
extern char **environ; // defined in unistd.h

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

static Value lxMapInit(int argCount, Value *args) {
    CHECK_ARITY("Map#init", 1, -1, argCount);
    callSuper(0, NULL, NULL);
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

static Value lxMapDup(int argCount, Value *args) {
    CHECK_ARITY("Map#dup", 1, 1, argCount);
    Value dup = callSuper(0, NULL, NULL);
    ObjInstance *dupObj = AS_INSTANCE(dup);
    Value orig = *args;
    Table *mapOrig = MAP_GETHIDDEN(orig);

    ObjInternal *internalMap = newInternalObject(
        NULL, sizeof(Table), markInternalMap, freeInternalMap
    );
    Table *mapDup = ALLOCATE(Table, 1);
    initTable(mapDup);
    internalMap->data = mapDup;
    tableSet(&dupObj->hiddenFields, OBJ_VAL(
        internedString("map", 3)), OBJ_VAL(internalMap));

    Entry e; int idx = 0;
    TABLE_FOREACH(mapOrig, e, idx) {
        tableSet(mapDup, e.key, e.value);
    }

    return dup;
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

static Value lxMapGet(int argCount, Value *args) {
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

static Value lxMapSet(int argCount, Value *args) {
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
    return createIterator(args[0]);
}

static Value lxMapEquals(int argCount, Value *args) {
    CHECK_ARITY("Map#==", 2, 2, argCount);
    return BOOL_VAL(mapEquals(args[0], args[1]));
}

// FIXME: figure out how to hash this properly
static Value lxMapHashKey(int argCount, Value *args) {
    CHECK_ARITY("Map#hashKey", 1, 1, argCount);
    Value self = *args;
    Obj *selfObj = AS_OBJ(self);
    uint32_t hash = 166779; // XXX: no reason for this number
    Table *map = MAP_GETHIDDEN(self);
    Entry e; int idx = 0;
    TABLE_FOREACH(map, e, idx) {
        if (AS_OBJ(e.key) == selfObj || AS_OBJ(e.value) == selfObj) { // avoid infinite recursion
            hash = hash ^ 16667; // XXX: no reason for this number
            continue;
        }
        hash = hash ^ (valHash(e.key) ^ valHash(e.value));
    }
    return NUMBER_VAL(hash);
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

static Value lxMapHasKey(int argCount, Value *args) {
    CHECK_ARITY("Map#hasKey", 2, 2, argCount);
    Value self = args[0];
    Value key = args[1];
    Table *map = MAP_GETHIDDEN(self);
    Value found;
    return BOOL_VAL(tableGet(map, key, &found));
}

static Value lxMapSlice(int argCount, Value *args) {
    CHECK_ARITY("Map#hasKey", 2, -1, argCount);
    Value self = args[0];
    Table *map = MAP_GETHIDDEN(self);
    Value ret = newMap();
    Table *mapRet = MAP_GETHIDDEN(ret);
    for (int i = 1; i < argCount; i++) {
        Value val;
        if (tableGet(map, args[i], &val)) {
            tableSet(mapRet, args[i], val);
        }
    }
    return ret;
}

static Value lxMapMerge(int argCount, Value *args) {
    CHECK_ARITY("Map#merge", 2, 2, argCount);
    Value self = args[0];
    Value other = args[1];
    Table *otherMap = MAP_GETHIDDEN(other);
    CHECK_ARG_IS_A(other, lxMapClass, 1);
    Value ret = callMethod(AS_OBJ(self), internedString("dup", 3), 0, NULL);
    Table *retMap = MAP_GETHIDDEN(ret);
    Entry e; int idx = 0;
    TABLE_FOREACH(otherMap, e, idx) {
        tableSet(retMap, e.key, e.value);
    }
    return ret;
}

static Value lxMapDelete(int argCount, Value *args) {
    CHECK_ARITY("Map#delete", 2, -1, argCount);
    Value self = args[0];
    Table *map = MAP_GETHIDDEN(self);
    int deleted = 0;
    for (int i = 1; i < argCount; i++) {
        if (tableDelete(map, args[i])) {
            deleted++;
        }
    }
    return NUMBER_VAL(deleted);
}

static Value lxMapRehash(int argCount, Value *args) {
    CHECK_ARITY("Map#rehash", 1, 1, argCount);
    Value self = args[0];
    ObjInstance *selfObj = AS_INSTANCE(self);
    Table *mapOld = MAP_GETHIDDEN(self);
    Table *mapNew = ALLOCATE(Table, 1);
    initTableWithCapa(mapNew, tableCapacity(mapOld));
    Entry e; int idx = 0;
    TABLE_FOREACH(mapOld, e, idx) {
        tableSet(mapNew, e.key, e.value);
    }
    Value internalVal;
    ASSERT(tableGet(&selfObj->hiddenFields, OBJ_VAL(internedString("map", 3)), &internalVal));
    ObjInternal *internal = AS_INTERNAL(internalVal);
    internal->data = mapNew;
    freeTable(mapOld);
    FREE(Table, mapOld);
    return self;
}

static Value lxMapGetSize(int argCount, Value *args) {
    Table *map = MAP_GETHIDDEN(*args);
    return NUMBER_VAL(map->count);
}

// ENV

static Value lxEnvGet(int argCount, Value *args) {
    CHECK_ARITY("ENV#[]", 2, 2, argCount);
    Value key = args[1];
    CHECK_ARG_IS_INSTANCE_OF(key, lxStringClass, 1);
    const char *ckey = VAL_TO_STRING(key)->chars;
    char *val = getenv(ckey);
    if (val == NULL) {
        return NIL_VAL;
    } else {
        return newStringInstance(copyString(val, strlen(val)));
    }
}

static Value lxEnvSet(int argCount, Value *args) {
    CHECK_ARITY("ENV#[]=", 3, 3, argCount);
    Value key = args[1];
    Value val = args[2];
    CHECK_ARG_IS_INSTANCE_OF(key, lxStringClass, 1);
    CHECK_ARG_IS_INSTANCE_OF(val, lxStringClass, 2);
    const char *ckey = VAL_TO_STRING(key)->chars;
    const char *cval = VAL_TO_STRING(val)->chars;
    int last = errno;
    if (setenv(ckey, cval, 1) != 0) {
        int err = errno;
        errno = last;
        throwErrorFmt(lxErrClass, "Error setting environment variable '%s': %s",
                ckey, strerror(err));
    }
    return val;
}

static Value createEnvMap(void) {
    char **envp = environ;
    Value mapVal = newMap();
    Table *map = MAP_GETHIDDEN(mapVal);
    while (*envp) {
        char *eq = strchr(*envp, '=');
        if (!eq) {
            throwErrorFmt(lxErrClass, "Invalid environment variable found: '%s'. "
                    "Contains no '='?", *envp);
        }
        size_t varLen = strlen(*envp);
        ObjString *nameStr = copyString(*envp, (int)(eq-*envp));
        ObjString *valStr = copyString(eq+1, (*envp+varLen)-eq-1);
        Value name = newStringInstance(nameStr);
        Value val = newStringInstance(valStr);
        tableSet(map, name, val);
        envp++;
    }
    return mapVal;
}

static Value lxEnvAll(int argCount, Value *args) {
    CHECK_ARITY("ENV#all", 1, 1, argCount);
    return createEnvMap();
}

static Value lxEnvIter(int argCount, Value *args) {
    CHECK_ARITY("ENV#iter", 1, 1, argCount);
    return createIterator(createEnvMap());
}

static Value lxEnvDelete(int argCount, Value *args) {
    CHECK_ARITY("ENV#delete", 2, -1, argCount);
    for (int i = 1; i < argCount; i++) {
        Value name = args[i];
        CHECK_ARG_IS_INSTANCE_OF(name, lxStringClass, i);
        const char *cname = VAL_TO_STRING(name)->chars;
        int last = errno;
        if (unsetenv(cname) != 0) {
            int err = errno;
            errno = last;
            throwErrorFmt(lxErrClass, "Error deleting environment variable '%s': %s",
                    cname, strerror(err));
        }
    }
    return BOOL_VAL(true);
}

void Init_MapClass() {
    ObjClass *mapClass = addGlobalClass("Map", lxObjClass);
    lxMapClass = mapClass;

    nativeMapInit = addNativeMethod(mapClass, "init", lxMapInit);
    // methods
    addNativeMethod(mapClass, "dup", lxMapDup);
    addNativeMethod(mapClass, "opIndexGet", lxMapGet);
    addNativeMethod(mapClass, "opIndexSet", lxMapSet);
    addNativeMethod(mapClass, "opEquals", lxMapEquals);
    addNativeMethod(mapClass, "hashKey", lxMapHashKey);
    addNativeMethod(mapClass, "keys", lxMapKeys);
    addNativeMethod(mapClass, "values", lxMapValues);
    addNativeMethod(mapClass, "toString", lxMapToString);
    addNativeMethod(mapClass, "iter", lxMapIter);
    addNativeMethod(mapClass, "clear", lxMapClear);
    addNativeMethod(mapClass, "hasKey", lxMapHasKey);
    addNativeMethod(mapClass, "slice", lxMapSlice);
    addNativeMethod(mapClass, "merge", lxMapMerge);
    addNativeMethod(mapClass, "delete", lxMapDelete);
    addNativeMethod(mapClass, "rehash", lxMapRehash);

    // getters
    addNativeGetter(mapClass, "size", lxMapGetSize);

    lxEnvClass = newClass(internedString("ENV", 3), lxObjClass);
    lxEnv = newInstance(lxEnvClass);

    addNativeMethod(lxEnvClass, "opIndexGet", lxEnvGet);
    addNativeMethod(lxEnvClass, "opIndexSet", lxEnvSet);
    addNativeMethod(lxEnvClass, "all", lxEnvAll);
    addNativeMethod(lxEnvClass, "delete", lxEnvDelete);
    addNativeMethod(lxEnvClass, "iter", lxEnvIter);

    tableSet(&vm.globals, OBJ_VAL(internedString("ENV", 3)), OBJ_VAL(lxEnv));
}
