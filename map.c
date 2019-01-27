#include <unistd.h>
#include <errno.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"
#include "compiler.h"

ObjClass *lxMapClass = NULL;
ObjNative *nativeMapInit = NULL;
static ObjString *mapStr = NULL;
ObjClass *lxEnvClass = NULL;
ObjInstance *lxEnv = NULL;
extern char **environ; // defined in unistd.h

static Value lxMapInit(int argCount, Value *args) {
    CHECK_ARITY("Map#init", 1, -1, argCount);
    callSuper(0, NULL, NULL);
    Value self = args[0];
    ObjMap *selfObj = AS_MAP(self);
    Table *map = selfObj->table;

    if (argCount == 1) {
        return self;
    }

    if (argCount == 2) {
        Value ary = args[1];
        CHECK_ARG_IS_INSTANCE_OF(ary, lxAryClass, 1);
        ObjArray *aryObj = AS_ARRAY(ary);
        ValueArray *aryInt = &aryObj->valAry;
        for (int i = 0; i < aryInt->count; i++) {
            Value el = aryInt->values[i];
            if (UNLIKELY(!IS_AN_ARRAY(el))) {
                throwErrorFmt(lxTypeErrClass, "Expected array element to be an array of length 2, got a: %s", typeOfVal(el));
            }
            if (UNLIKELY(ARRAY_SIZE(el) != 2)) {
                throwArgErrorFmt("Wrong array size given, expected 2, got: %d",
                        ARRAY_SIZE(el));
            }
            Value mapKey = ARRAY_GET(el, 0);
            Value mapVal = ARRAY_GET(el, 1);
            tableSet(map, mapKey, mapVal);
            OBJ_WRITE(self, mapKey);
            OBJ_WRITE(self, mapVal);
        }
    } else {
        throwArgErrorFmt("Expected 1 argument, got %d", argCount-1);
    }
    return self;
}

static Value lxMapDup(int argCount, Value *args) {
    CHECK_ARITY("Map#dup", 1, 1, argCount);
    Value dup = callSuper(0, NULL, NULL);
    ObjMap *dupObj = AS_MAP(dup);
    Value orig = *args;
    Table *mapOrig = AS_MAP(orig)->table;
    Table *mapDup = dupObj->table;

    Entry e; int idx = 0;
    TABLE_FOREACH(mapOrig, e, idx, {
        tableSet(mapDup, e.key, e.value);
        OBJ_WRITE(dup, e.key);
        OBJ_WRITE(dup, e.value);
    })

    return dup;
}

static Value lxMapToString(int argCount, Value *args) {
    CHECK_ARITY("Map#toString", 1, 1, argCount);
    Value self = args[0];
    Obj *selfObj = AS_OBJ(self);
    Value ret = OBJ_VAL(copyString("{", 1, NEWOBJ_FLAG_NONE));
    ObjString *bufRet = AS_STRING(ret);
    Table *map = AS_MAP(self)->table;
    Entry e; int idx = 0;
    int sz = map->count; int i = 0;
    TABLE_FOREACH(map, e, idx, {
        if (IS_OBJ(e.key) && AS_OBJ(e.key) == selfObj) {
            pushCString(bufRet, "{...}", 5);
        } else {
            ObjString *keyS = valueToString(e.key, copyString, NEWOBJ_FLAG_NONE);
            pushCString(bufRet, keyS->chars, strlen(keyS->chars));
        }
        pushCString(bufRet, " => ", 4);
        if (IS_OBJ(e.value) && AS_OBJ(e.value) == selfObj) {
            pushCString(bufRet, "{...}", 5);
        } else {
            ObjString *valS = valueToString(e.value, copyString, NEWOBJ_FLAG_NONE);
            pushCString(bufRet, valS->chars, strlen(valS->chars));
        }

        if (i < (sz-1)) {
            pushCString(bufRet, ", ", 2);
        }
        i++;
    })

    pushCString(bufRet, "}", 1);

    return ret;
}

static Value lxMapGet(int argCount, Value *args) {
    CHECK_ARITY("Map#[]", 2, 2, argCount);
    Value self = args[0];
    Table *map = AS_MAP(self)->table;
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
    ObjMap *selfObj = AS_MAP(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Map is frozen, cannot modify");
    }
    Table *map = selfObj->table;
    Value key = args[1];
    Value val = args[2];
    tableSet(map, key, val);
    OBJ_WRITE(self, key);
    OBJ_WRITE(self, val);
    return val;
}

static Value lxMapKeys(int argCount, Value *args) {
    CHECK_ARITY("Map#keys", 1, 1, argCount);
    Value self = args[0];
    Table *map = AS_MAP(self)->table;
    Entry entry; int i = 0;
    Value ary = newArray();
    TABLE_FOREACH(map, entry, i, {
        arrayPush(ary, entry.key);
    })
    return ary;
}

static Value lxMapValues(int argCount, Value *args) {
    CHECK_ARITY("Map#values", 1, 1, argCount);
    Value self = args[0];
    Table *map = AS_MAP(self)->table;
    Entry entry; int i = 0;
    Value ary = newArray();
    TABLE_FOREACH(map, entry, i, {
        arrayPush(ary, entry.value);
    })
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
    Table *map = AS_MAP(self)->table;
    Entry e; int idx = 0;
    TABLE_FOREACH(map, e, idx, {
        if (AS_OBJ(e.key) == selfObj || AS_OBJ(e.value) == selfObj) { // avoid infinite recursion
            hash = hash ^ 16667; // XXX: no reason for this number
            continue;
        }
        hash = hash ^ (valHash(e.key) ^ valHash(e.value));
    })
    return NUMBER_VAL(hash);
}

static Value lxMapClear(int argCount, Value *args) {
    CHECK_ARITY("Map#clear", 1, 1, argCount);
    Value self = args[0];
    ObjMap *selfObj = AS_MAP(self);
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
    Table *map = AS_MAP(self)->table;
    Value found;
    return BOOL_VAL(tableGet(map, key, &found));
}

static Value lxMapSlice(int argCount, Value *args) {
    CHECK_ARITY("Map#hasKey", 2, -1, argCount);
    Value self = args[0];
    Table *map = AS_MAP(self)->table;
    Value ret = newMap();
    Table *mapRet = AS_MAP(ret)->table;
    for (int i = 1; i < argCount; i++) {
        Value val;
        if (tableGet(map, args[i], &val)) {
            tableSet(mapRet, args[i], val);
            OBJ_WRITE(ret, args[i]);
            OBJ_WRITE(ret, val);
        }
    }
    return ret;
}

// Returns a new Map, with the key-values from `self` and `other`.
// If both contain the same key, the value from `other` is taken.
static Value lxMapMerge(int argCount, Value *args) {
    CHECK_ARITY("Map#merge", 2, 2, argCount);
    Value self = args[0];
    Value other = args[1];
    Table *otherMap = AS_MAP(other)->table;
    CHECK_ARG_IS_A(other, lxMapClass, 1);
    Value ret = callMethod(AS_OBJ(self), INTERN("dup"), 0, NULL, NULL);
    Table *retMap = AS_MAP(ret)->table;
    Entry e; int idx = 0;
    TABLE_FOREACH(otherMap, e, idx, {
        tableSet(retMap, e.key, e.value);
        OBJ_WRITE(other, e.key);
        OBJ_WRITE(other, e.value);
    });
    return ret;
}

// See `Map#merge`, but modifies receiver instead of returning new Map.
static Value lxMapMergeWith(int argCount, Value *args) {
    CHECK_ARITY("Map#mergeWith", 2, 2, argCount);
    Value self = args[0];
    Value other = args[1];
    Table *myMap = AS_MAP(self)->table;
    Table *otherMap = AS_MAP(other)->table;
    CHECK_ARG_IS_A(other, lxMapClass, 1);
    if (isFrozen(AS_OBJ(self))) {
        throwErrorFmt(lxErrClass, "%s", "Map is frozen, cannot modify");
    }
    Entry e; int idx = 0;
    TABLE_FOREACH(otherMap, e, idx, {
        tableSet(myMap, e.key, e.value);
        OBJ_WRITE(self, e.key);
        OBJ_WRITE(self, e.value);
    })
    return self;
}

static Value lxMapDelete(int argCount, Value *args) {
    CHECK_ARITY("Map#delete", 2, -1, argCount);
    Value self = args[0];
    Table *map = AS_MAP(self)->table;
    if (isFrozen(AS_OBJ(self))) {
        throwErrorFmt(lxErrClass, "%s", "Map is frozen, cannot modify");
    }
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
    ObjMap *selfObj = AS_MAP(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Map is frozen, cannot modify");
    }
    Table *mapOld = selfObj->table;
    Table *mapNew = ALLOCATE(Table, 1);
    initTableWithCapa(mapNew, tableCapacity(mapOld));
    Entry e; int idx = 0;
    TABLE_FOREACH(mapOld, e, idx, {
        tableSet(mapNew, e.key, e.value);
    })
    freeTable(mapOld);
    FREE(Table, mapOld);
    selfObj->table = mapNew;
    return self;
}

static Value lxMapGetSize(int argCount, Value *args) {
    Table *map = AS_MAP(*args)->table;
    return NUMBER_VAL(map->count);
}

static Value lxMapEach(int argCount, Value *args) {
    CHECK_ARITY("Map#each", 1, 1, argCount);
    volatile LxThread *th = vm.curThread;
    Value self = *args;
    volatile Table *map = AS_MAP(self)->table;
    volatile int status = 0;
    volatile int startIdx = 0;
    volatile BlockIterFunc fn = getFrame()->callInfo->blockIterFunc;
    volatile Value yieldArgs[2];
    volatile Obj *block = NULL;
    volatile ObjInstance *blockInstance = NULL;
    blockInstance = getBlockArg(getFrame());
    if (blockInstance) {
        block = blockCallableBlock(OBJ_VAL(blockInstance));
    }
    if (!block && getFrame()->callInfo) {
        block = (Obj*)(getFrame()->callInfo->blockFunction);
    }
    if (!block) {
        throwErrorFmt(lxErrClass, "no block given");
    }
    volatile BlockStackEntry *bentry = NULL;
    while (true) {
        if (startIdx == map->count) {
            return *args;
        }
        SETUP_BLOCK(block, bentry, status, th->errInfo)
        if (status == TAG_NONE) {
            break;
        } else if (status == TAG_RAISE) {
            int iterFlags = 0;
            ObjInstance *errInst = AS_INSTANCE(th->lastErrorThrown);
            ASSERT(errInst);
            if (errInst->klass == lxBreakBlockErrClass) {
                return NIL_VAL;
            } else if (errInst->klass == lxContinueBlockErrClass) {
                Value retVal = getProp(th->lastErrorThrown, INTERN("ret"));
                if (fn) {
                    fn(2, (Value*)yieldArgs, retVal, getFrame()->callInfo, &iterFlags);
                }
            } else if (errInst->klass == lxReturnBlockErrClass) {
                Value retVal = getProp(th->lastErrorThrown, INTERN("ret"));
                if (fn) {
                    fn(2, (Value*)yieldArgs, retVal, getFrame()->callInfo, &iterFlags);
                } else {
                    return retVal;
                }
            } else {
                throwError(th->lastErrorThrown);
            }
        }
    }

    Entry e;
    TABLE_FOREACH_IDX(map, e, startIdx, {
        startIdx++;
        yieldArgs[0] = e.key;
        yieldArgs[1] = e.value;
        yieldFromC(2, (Value*)yieldArgs, TO_INSTANCE(blockInstance));
    })
    return self;
}

static void mapIter(int argCount, Value *args, Value ret, CallInfo *cinfo, int *iterFlags) {
    arrayPush(*cinfo->blockIterRet, ret);
}

static Value lxMapMap(int argCount, Value *args) {
    CHECK_ARITY("Map#map", 1, 1, argCount);
    Value self = *args;

    volatile Value ret = newArray();
    CallInfo cinfo;
    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.blockIterFunc = mapIter;
    cinfo.blockIterRet = &ret;
    cinfo.blockFunction = getFrame()->callInfo->blockFunction;
    cinfo.blockInstance = getBlockArg(getFrame());
    Value res = callMethod(AS_OBJ(self), INTERN("each"), 0, NULL, &cinfo);
    if (IS_NIL(res)) {
        return res;
    } else {
        return ret;
    }
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
        return OBJ_VAL(copyString(val, strlen(val), NEWOBJ_FLAG_NONE));
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
        throwErrorFmt(sysErrClass(err), "Error setting environment variable '%s': %s",
                ckey, strerror(err));
    }
    return val;
}

static Value createEnvMap(void) {
    char **envp = environ;
    Value mapVal = newMap();
    Table *map = AS_MAP(mapVal)->table;
    while (*envp) {
        char *eq = strchr(*envp, '=');
        if (!eq) {
            throwErrorFmt(lxErrClass, "Invalid environment variable found: '%s'. "
                    "Contains no '='?", *envp);
        }
        size_t varLen = strlen(*envp);
        ObjString *nameStr = copyString(*envp, (int)(eq-*envp), NEWOBJ_FLAG_NONE);
        ObjString *valStr = copyString(eq+1, (*envp+varLen)-eq-1, NEWOBJ_FLAG_NONE);
        tableSet(map, OBJ_VAL(nameStr), OBJ_VAL(valStr));
        OBJ_WRITE(mapVal, OBJ_VAL(nameStr));
        OBJ_WRITE(mapVal, OBJ_VAL(valStr));
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
            throwErrorFmt(sysErrClass(err), "Error deleting environment variable '%s': %s",
                    cname, strerror(err));
        }
    }
    return BOOL_VAL(true);
}

void Init_MapClass() {
    mapStr = INTERNED("map", 3);

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
    addNativeMethod(mapClass, "mergeWith", lxMapMergeWith);
    addNativeMethod(mapClass, "delete", lxMapDelete);
    addNativeMethod(mapClass, "rehash", lxMapRehash);
    addNativeMethod(mapClass, "each", lxMapEach);
    addNativeMethod(mapClass, "map", lxMapMap);

    // getters
    addNativeGetter(mapClass, "size", lxMapGetSize);

    lxEnvClass = newClass(INTERNED("ENV", 3), lxObjClass, NEWOBJ_FLAG_OLD);
    lxEnv = newInstance(lxEnvClass, NEWOBJ_FLAG_OLD);

    addNativeMethod(lxEnvClass, "opIndexGet", lxEnvGet);
    addNativeMethod(lxEnvClass, "opIndexSet", lxEnvSet);
    addNativeMethod(lxEnvClass, "all", lxEnvAll);
    addNativeMethod(lxEnvClass, "delete", lxEnvDelete);
    addNativeMethod(lxEnvClass, "iter", lxEnvIter);

    tableSet(&vm.constants, OBJ_VAL(INTERNED("ENV", 3)), OBJ_VAL(lxEnv));
}
