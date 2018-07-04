#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include "runtime.h"
#include "object.h"
#include "memory.h"
#include "debug.h"
#include "compiler.h"
#include "vm.h"

const char pathSeparator =
#ifdef _WIN32
                            '\\';
#else
                            '/';
#endif

// TODO
#define CHECK_ARG_TYPE(...)

static bool fileExists(char *fname) {
    struct stat buffer;
    return (stat(fname, &buffer) == 0);
}

Value lxClock(int argCount, Value *args) {
    CHECK_ARGS("clock", 0, 0, argCount);
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

Value lxTypeof(int argCount, Value *args) {
    CHECK_ARGS("typeof", 1, 1, argCount);
    const char *strType = typeOfVal(*args);
    return OBJ_VAL(newStackString(strType, strlen(strType)));
}

Value lxDebugger(int argCount, Value *args) {
    CHECK_ARGS("debugger", 0, 0, argCount);
    vm.debugger.awaitingPause = true;
    return NIL_VAL;
}

Value lxEval(int argCount, Value *args) {
    CHECK_ARGS("eval", 1, 1, argCount);
    Value src = *args;
    CHECK_ARG_TYPE(src, VAL_T_OBJ, IS_STRING, 1);
    char *csrc = AS_CSTRING(src);
    if (strlen(csrc) == 0) {
        return NIL_VAL;
    }
    return VMEval(csrc, "(eval)", 1);
}

static Value loadScriptHelper(Value fname, const char *funcName, bool checkLoaded) {
    char *cfile = AS_CSTRING(fname);
    bool isAbsFile = cfile[0] == pathSeparator;
    char pathbuf[300] = { '\0' };
    bool fileFound = false;
    if (isAbsFile) {
        memcpy(pathbuf, cfile, strlen(cfile));
        fileFound = true;
    } else {
        Value el; int i = 0;
        LXARRAY_FOREACH(lxLoadPath, el, i) {
            if (!IS_STRING(el)) continue;
            char *dir = AS_CSTRING(el);
            memset(pathbuf, 0, 300);
            memcpy(pathbuf, dir, strlen(dir));
            if (strcmp(pathbuf, ".") == 0) {
                char *cwdres = getcwd(pathbuf, 250);
                if (cwdres == NULL) {
                    fprintf(stderr,
                        "Couldn't get current working directory for loading script!"
                        " Maybe too long?\n");
                    continue;
                }
            }
            if (dir[strlen(dir)-1] != pathSeparator) {
                strncat(pathbuf, &pathSeparator, 1);
            }
            strcat(pathbuf, cfile);
            if (!fileExists(pathbuf)) {
                continue;
            }
            fileFound = true;
            break;
        }
    }
    if (fileFound) {
        if (checkLoaded && VMLoadedScript(pathbuf)) {
            return BOOL_VAL(false);
        }
        Chunk chunk;
        initChunk(&chunk);
        CompileErr err = COMPILE_ERR_NONE;
        int compile_res = compile_file(pathbuf, &chunk, &err);
        if (compile_res != 0) {
            // TODO: throw syntax error
            return BOOL_VAL(false);
        }
        ObjString *fpath = newString(pathbuf, strlen(pathbuf));
        if (checkLoaded) {
            vec_push(&vm.loadedScripts, OBJ_VAL(fpath));
        }
        InterpretResult ires = loadScript(&chunk, pathbuf);
        return BOOL_VAL(ires == INTERPRET_OK);
    } else {
        fprintf(stderr, "File '%s' not found (%s)\n", cfile, funcName);
        return BOOL_VAL(false);
    }
}

Value lxRequireScript(int argCount, Value *args) {
    CHECK_ARGS("requireScript", 1, 1, argCount);
    Value fname = *args;
    CHECK_ARG_TYPE(fname, VAL_T_OBJ, IS_STRING, 1);
    return loadScriptHelper(fname, "requireScript", true);
}

Value lxLoadScript(int argCount, Value *args) {
    CHECK_ARGS("loadScript", 1, 1, argCount);
    Value fname = *args;
    CHECK_ARG_TYPE(fname, VAL_T_OBJ, IS_STRING, 1);
    return loadScriptHelper(fname, "loadScript", false);
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
    ASSERT(IS_AN_ARRAY(self));
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(NULL, markInternalAry, freeInternalAry);
    ValueArray *ary = ALLOCATE(ValueArray, 1);
    initValueArray(ary);
    internalObj->data = ary;
    tableSet(&selfObj->hiddenFields, OBJ_VAL(copyString("ary", 3)), OBJ_VAL(internalObj));
    for (int i = 1; i < argCount; i++) {
        writeValueArray(ary, args[i]);
    }
    ASSERT(ary->count == argCount-1);
    return self;
}

// a.push(1);
Value lxArrayPush(int argCount, Value *args) {
    CHECK_ARGS("Array#push", 2, 2, argCount);
    Value self = args[0];
    arrayPush(self, args[1]);
    return self;
}

// print a;
// OR
// a.toString(); // => [1,2,3]
Value lxArrayToString(int argCount, Value *args) {
    CHECK_ARGS("Array#toString", 1, 1, argCount);
    Value self = *args;
    ASSERT(IS_AN_ARRAY(self));
    Obj* selfObj = AS_OBJ(self);
    ObjString *ret = newStackString("[", 1);
    ValueArray *ary = ARRAY_GETHIDDEN(self);
    for (int i = 0; i < ary->count; i++) {
        Value elVal = ary->values[i];
        if (IS_OBJ(elVal) && (AS_OBJ(elVal) == selfObj)) {
            pushCString(ret, "[...]", 5);
            continue;
        }
        if (IS_OBJ(elVal)) {
            ASSERT(AS_OBJ(elVal)->type > OBJ_T_NONE);
        }
        ObjString *buf = valueToString(elVal, newStackString);
        pushCString(ret, buf->chars, strlen(buf->chars));
        if (i < (ary->count-1)) {
            pushCString(ret, ",", 1);
        }
    }
    pushCString(ret, "]", 1);
    return OBJ_VAL(ret);
}


Value lxArrayIndexGet(int argCount, Value *args) {
    CHECK_ARGS("Array#[]", 2, 2, argCount);
    Value self = args[0];
    ASSERT(IS_AN_ARRAY(self));
    Value num = args[1];
    CHECK_ARG_TYPE(num, VAL_T_NUMBER, 1);
    ValueArray *ary = ARRAY_GETHIDDEN(self);
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
    ASSERT(IS_AN_ARRAY(self));
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
    CHECK_ARGS("Map#init", 1, -1, argCount);
    Value self = args[0];
    ASSERT(IS_A_MAP(self));
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalMap = newInternalObject(
        NULL, markInternalMap, freeInternalMap
    );
    Table *map = ALLOCATE(Table, 1);
    initTable(map);
    internalMap->data = map;
    tableSet(&selfObj->hiddenFields, OBJ_VAL(
        copyString("map", 3)), OBJ_VAL(internalMap));

    if (argCount == 1) {
        return self;
    }

    if (argCount == 2) {
        Value ary = args[1];
        CHECK_ARG_OBJ_TYPE(ary, lxAryClass, 1);
        ValueArray *aryInt = ARRAY_GETHIDDEN(ary);
        for (int i = 0; i < aryInt->count; i++) {
            Value el = aryInt->values[i];
            // FIXME: throw error
            ASSERT(IS_AN_ARRAY(el));
            if (ARRAY_SIZE(el) != 2) {
                fprintf(stderr, "Wrong array size given, expected 2");
                ASSERT(0);
            }
            Value mapKey = ARRAY_GET(el, 0);
            Value mapVal = ARRAY_GET(el, 1);
            ASSERT(tableSet(map, mapKey, mapVal));
        }
    } else {
        throwArgErrorFmt("Expected 1 argument, got %d", argCount-1);
    }
    return self;
}

Value lxMapToString(int argCount, Value *args) {
    CHECK_ARGS("Map#toString", 1, 1, argCount);
    Value self = args[0];
    Obj *selfObj = AS_OBJ(self);
    ASSERT(IS_A_MAP(self));
    ObjString *ret = newStackString("{", 1);
    Table *map = MAP_GETHIDDEN(self);
    Entry e; int idx = 0;
    int sz = map->count; int i = 0;
    TABLE_FOREACH(map, e, idx) {
        if (IS_OBJ(e.key) && AS_OBJ(e.key) == selfObj) {
            pushCString(ret, "{...}", 5);
        } else {
            ObjString *keyS = valueToString(e.key, newStackString);
            pushCString(ret, keyS->chars, strlen(keyS->chars));
        }
        pushCString(ret, " => ", 4);
        if (IS_OBJ(e.value) && AS_OBJ(e.value) == selfObj) {
            pushCString(ret, "{...}", 5);
        } else {
            ObjString *valS = valueToString(e.value, newStackString);
            pushCString(ret, valS->chars, strlen(valS->chars));
        }

        if (i < (sz-1)) {
            pushCString(ret, ", ", 2);
        }
        i++;
    }

    pushCString(ret, "}", 1);

    return OBJ_VAL(ret);
}

Value lxMapIndexGet(int argCount, Value *args) {
    CHECK_ARGS("Map#indexGet", 2, 2, argCount);
    Value self = args[0];
    ASSERT(IS_A_MAP(self));
    Table *map = MAP_GETHIDDEN(self);
    Value key = args[1];
    Value found;
    if (tableGet(map, key, &found)) {
        return found;
    } else {
        return NIL_VAL;
    }
}

Value lxMapIndexSet(int argCount, Value *args) {
    CHECK_ARGS("Map#indexSet", 3, 3, argCount);
    Value self = args[0];
    ASSERT(IS_A_MAP(self));
    Table *map = MAP_GETHIDDEN(self);
    Value key = args[1];
    Value val = args[2];
    tableSet(map, key, val);
    return val;
}

Value lxMapKeys(int argCount, Value *args) {
    CHECK_ARGS("Map#keys", 1, 1, argCount);
    Value self = args[0];
    ASSERT(IS_A_MAP(self));
    Table *map = MAP_GETHIDDEN(self);
    Entry entry; int i = 0;
    Value ary = newArray();
    TABLE_FOREACH(map, entry, i) {
        arrayPush(ary, entry.key);
    }
    return ary;
}

Value lxMapValues(int argCount, Value *args) {
    CHECK_ARGS("Map#values", 1, 1, argCount);
    Value self = args[0];
    ASSERT(IS_A_MAP(self));
    Table *map = MAP_GETHIDDEN(self);
    Entry entry; int i = 0;
    Value ary = newArray();
    TABLE_FOREACH(map, entry, i) {
        arrayPush(ary, entry.value);
    }
    return ary;
}

Value lxErrInit(int argCount, Value *args) {
    CHECK_ARGS("Error#init", 1, 2, argCount);
    Value self = args[0];
    ASSERT(IS_AN_ERROR(self));
    Value msg;
    if (argCount == 2) {
        msg = args[1];
    } else {
        msg = NIL_VAL;
    }
    setProp(self, copyString("message", 7), msg);
    return self;
}

bool runtimeCheckArgs(int min, int max, int actual) {
    return min <= actual && (max >= actual || max == -1);
}

