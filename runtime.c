#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include "runtime.h"
#include "object.h"
#include "value.h"
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

// ex: CHECK_ARG_BUILTIN_TYPE(value, IS_BOOL_FUNC, "bool", 1);
#define CHECK_ARG_BUILTIN_TYPE(value, typechk_p, typename, argnum) check_builtin_arg_type(value, typechk_p, typename, argnum)
#define CHECK_ARG_IS_INSTANCE_OF(value, klass, argnum) check_arg_is_instance_of(value, klass, argnum)
#define CHECK_ARG_IS_A(value, klass, argnum) check_arg_is_a(value, klass, argnum)

static void check_builtin_arg_type(Value arg, value_type_p typechk_p, const char *typeExpect, int argnum) {
    if (!typechk_p(arg)) {
        const char *typeActual = typeOfVal(arg);
        throwArgErrorFmt("Expected argument %d to be a %s, got: %s", argnum, typeExpect, typeActual);
    }
}
static void check_arg_is_instance_of(Value arg, ObjClass *klass, int argnum) {
    const char *typeExpect = klass->name->chars;
    if (!is_value_instance_of_p(arg, klass)) {
        const char *typeActual;
        if (IS_INSTANCE(arg)) {
            ObjString *className = AS_INSTANCE(arg)->klass->name;
            typeActual = className ? className->chars : "(anon)";
        } else {
            typeActual = typeOfVal(arg);
        }
        throwArgErrorFmt("Expected argument %d to be of exact class %s, got: %s", argnum, typeExpect, typeActual);
    }
}
static void check_arg_is_a(Value arg, ObjClass *klass, int argnum) {
    const char *typeExpect = klass->name->chars;
    if (!is_value_a_p(arg, klass)) {
        const char *typeActual;
        if (IS_INSTANCE(arg)) {
            ObjString *className = AS_INSTANCE(arg)->klass->name;
            typeActual = className ? className->chars : "(anon)";
        } else {
            typeActual = typeOfVal(arg);
        }
        throwArgErrorFmt("Expected argument %d to be of type %s, got: %s", argnum, typeExpect, typeActual);
    }
}

// Does this file exist and is it readable?
static bool fileReadable(char *fname) {
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
    return newStringInstance(copyString(strType, strlen(strType)));
}

Value lxDebugger(int argCount, Value *args) {
    CHECK_ARGS("debugger", 0, 0, argCount);
    vm.debugger.awaitingPause = true;
    return NIL_VAL;
}

Value lxEval(int argCount, Value *args) {
    CHECK_ARGS("eval", 1, 1, argCount);
    Value src = *args;
    CHECK_ARG_IS_A(src, lxStringClass, 1);
    char *csrc = VAL_TO_STRING(src)->chars;
    if (strlen(csrc) == 0) {
        return NIL_VAL;
    }
    return VMEval(csrc, "(eval)", 1);
}

static void enteredNewThread() {
    Value thread = newThread();
    threadSetStatus(thread, THREAD_RUNNING);
    threadSetId(thread, pthread_self());
    vm.curThread = AS_INSTANCE(thread);
    arrayPush(OBJ_VAL(vm.threads), thread);
    // TODO: set other threads to STOPPED?
}

static void exitingThread() {
    threadSetStatus(OBJ_VAL(vm.curThread), THREAD_STOPPED);
    arrayDelete(OBJ_VAL(vm.threads), OBJ_VAL(vm.curThread));
    vm.curThread = NULL;
}

static void *runCallableInNewThread(void *arg) {
    ObjClosure *closure = arg;
    ASSERT(closure);
    acquireGVL();
    THREAD_DEBUG(2, "in new thread");
    enteredNewThread();
    push(OBJ_VAL(closure));
    THREAD_DEBUG(2, "calling callable");
    callCallable(OBJ_VAL(closure), 0, false, NULL);
    vm_run(false);
    exitingThread();
    THREAD_DEBUG(2, "exiting new thread");
    releaseGVL();
    return AS_OBJ(pop());
}

Value lxNewThread(int argCount, Value *args) {
    CHECK_ARGS("newThread", 1, 1, argCount);
    Value closure = *args;
    CHECK_ARG_BUILTIN_TYPE(closure, IS_CLOSURE_FUNC, "closure", 1);
    ObjClosure *func = AS_CLOSURE(closure);
    pthread_t tnew;
    if (pthread_create(&tnew, NULL, runCallableInNewThread, func) == 0) {
        THREAD_DEBUG(2, "created thread id %lu", (unsigned long)tnew);
        releaseGVL();
        acquireGVL();
        return NUMBER_VAL((unsigned long)tnew);
    } else {
        THREAD_DEBUG(1, "Error creating new thread");
        // TODO: throw error
        return NIL_VAL;
    }
}

Value lxJoinThread(int argCount, Value *args) {
    CHECK_ARGS("joinThread", 1, 1, argCount);
    Value tidNum = *args;
    CHECK_ARG_BUILTIN_TYPE(tidNum, IS_NUMBER_FUNC, "number", 1);
    double num = AS_NUMBER(tidNum);
    THREAD_DEBUG(2, "Joining thread id %lu\n", (unsigned long)num);
    releaseGVL();
    int ret = 0;
    if ((ret = pthread_join((pthread_t)num, NULL)) != 0) {
        // TODO: throw error
        THREAD_DEBUG(1, "Error joining thread: (ret=%d)", ret);
    }
    // blocks
    acquireGVL();
    return NIL_VAL;
}

Value lxThreadInit(int argCount, Value *args) {
    CHECK_ARGS("Thread#init", 1, 1, argCount);
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(NULL, NULL, NULL);
    LxThread *th = calloc(sizeof(LxThread), 1); // GCed by default GC free of internalObject
    ASSERT_MEM(th);
    internalObj->data = th;
    tableSet(&selfObj->hiddenFields, OBJ_VAL(internedString("th", 2)), OBJ_VAL(internalObj));
    return self;
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
            if (dir[strlen(dir)-1] != pathSeparator) { // add trailing '/'
                strncat(pathbuf, &pathSeparator, 1);
            }
            strcat(pathbuf, cfile);
            if (!fileReadable(pathbuf)) {
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
        ObjString *fpath = copyString(pathbuf, strlen(pathbuf));
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
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    return loadScriptHelper(fname, "requireScript", true);
}

Value lxLoadScript(int argCount, Value *args) {
    CHECK_ARGS("loadScript", 1, 1, argCount);
    Value fname = *args;
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
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

// ex: var o = Object(); print o._class;
Value lxObjectGetClass(int argCount, Value *args) {
    Value self = *args;
    ObjClass *klass = AS_INSTANCE(self)->klass;
    if (klass) {
        return OBJ_VAL(klass);
    } else {
        return NIL_VAL;
    }
}

// ex: print o.objectId
Value lxObjectGetObjectId(int argCount, Value *args) {
    Value self = *args;
    size_t objId = AS_OBJ(self)->objectId;
    return NUMBER_VAL((double)objId);
}

// ex: var m = Module("MyMod");
Value lxModuleInit(int argCount, Value *args) {
    // TODO: add to module map, and make sure module doesn't already exist, maybe
    Value self = *args;
    CHECK_ARGS("Module#init", 1, 2, argCount);
    if (argCount == 1) { return self; }
    Value name = args[1];
    CHECK_ARG_IS_A(name, lxStringClass, 1);
    ObjModule *mod = AS_MODULE(self);
    Value nameStr = dupStringInstance(name);
    mod->name = VAL_TO_STRING(nameStr);
    return self;
}

// ex: var c = Class("MyClass", Object);
Value lxClassInit(int argCount, Value *args) {
    CHECK_ARGS("Class#init", 1, 3, argCount);
    Value self = *args;
    ObjClass *klass = AS_CLASS(self);
    if (argCount == 1) {
        klass->name = NULL;
        klass->superclass = lxObjClass;
        return self;
    }
    Value arg1 = args[1];
    ObjString *name = NULL;
    ObjClass *superClass = NULL;
    if (IS_A_STRING(arg1)) {
        name = VAL_TO_STRING(dupStringInstance(arg1));
    } else if (IS_CLASS(arg1)) {
        superClass = AS_CLASS(arg1);
    } else {
        throwArgErrorFmt("Expected argument 1 to be String or Class, got: %s", typeOfVal(arg1));
    }
    if (argCount == 3 && !superClass) {
        CHECK_ARG_IS_INSTANCE_OF(args[2], lxClassClass, 2);
        superClass = AS_CLASS(args[2]);
    }
    klass->name = name;
    klass->superclass = superClass;
    return self;
}

// ex: Object.include(Mod)
Value lxClassInclude(int argCount, Value *args) {
    CHECK_ARGS("Class#include", 2, 2, argCount);
    Value self = args[0];
    ObjClass *klass = AS_CLASS(self);
    Value modVal = args[1];
    CHECK_ARG_BUILTIN_TYPE(modVal, IS_MODULE_FUNC, "module", 1);
    ObjModule *mod = AS_MODULE(modVal);
    int alreadyIncluded = -1;
    vec_find(&klass->v_includedMods, mod, alreadyIncluded);
    if (alreadyIncluded == -1) {
        vec_push(&klass->v_includedMods, mod);
    }
    return modVal;
}

// Returns a copy of the class's name as a String
// ex: print Object.name
Value lxClassGetName(int argCount, Value *args) {
    CHECK_ARGS("Class#name", 1, 1, argCount);
    Value self = args[0];
    ObjClass *klass = AS_CLASS(self);
    ObjString *origName = klass->name;
    if (origName == NULL) {
        return newStringInstance(copyString("(anon)", 6));
    } else {
        return newStringInstance(dupString(origName));
    }
}

// ex: print Object._superClass;
Value lxClassGetSuperclass(int argCount, Value *args) {
    Value self = *args;
    ObjClass *klass = AS_CLASS(self);
    if (klass->superclass) {
        return OBJ_VAL(klass->superclass);
    } else {
        return NIL_VAL;
    }
}

// ex: var s = "string";
// ex: var s2 = String("string");
Value lxStringInit(int argCount, Value *args) {
    CHECK_ARGS("String#init", 1, 2, argCount);
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (argCount == 2) {
        Value internalStrVal = args[1];
        if (IS_T_STRING(internalStrVal)) {
            *args = internalStrVal;
            return internalStrVal;
        }
        ASSERT(IS_STRING(internalStrVal));
        tableSet(&selfObj->hiddenFields, OBJ_VAL(internedString("buf", 3)), internalStrVal);
        unhideFromGC(AS_OBJ(internalStrVal));
    } else {
        Value internalStrVal = OBJ_VAL(copyString("", 0));
        tableSet(&selfObj->hiddenFields, OBJ_VAL(internedString("buf", 3)), internalStrVal);
    }
    return self;
}

Value lxStringToString(int argCount, Value *args) {
    CHECK_ARGS("String#toString", 1, 1, argCount);
    return *args;
}

// ex: print("hi " + "there");
Value lxStringOpAdd(int argCount, Value *args) {
    CHECK_ARGS("String#opAdd", 2, 2, argCount);
    Value self = *args;
    Value rhs = args[1];
    Value ret = dupStringInstance(self);
    ObjString *lhsBuf = STRING_GETHIDDEN(ret);
    ASSERT(IS_T_STRING(rhs)); // TODO: throw error or coerce into String
    ObjString *rhsBuf = STRING_GETHIDDEN(rhs);
    pushString(lhsBuf, rhsBuf);
    return ret;
}

Value lxStringPush(int argCount, Value *args) {
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    CHECK_ARGS("String#push", 2, 2, argCount);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "String is frozen, cannot mutate");
    }
    Value rhs = args[1];
    CHECK_ARG_IS_A(rhs, lxStringClass, 1);
    ObjString *lhsBuf = STRING_GETHIDDEN(*args);
    ObjString *rhsBuf = STRING_GETHIDDEN(rhs);
    pushString(lhsBuf, rhsBuf);
    return *args;
}

// ex: var a = Array();
Value lxArrayInit(int argCount, Value *args) {
    CHECK_ARGS("Array#init", 1, -1, argCount);
    Value self = *args;
    ASSERT(IS_AN_ARRAY(self));
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(NULL, markInternalAry, freeInternalAry);
    ValueArray *ary = ALLOCATE(ValueArray, 1);
    initValueArray(ary);
    internalObj->data = ary;
    tableSet(&selfObj->hiddenFields, OBJ_VAL(internedString("ary", 3)), OBJ_VAL(internalObj));
    for (int i = 1; i < argCount; i++) {
        writeValueArray(ary, args[i]);
    }
    ASSERT(ary->count == argCount-1);
    return self;
}

// ex: a.push(1);
Value lxArrayPush(int argCount, Value *args) {
    CHECK_ARGS("Array#push", 2, 2, argCount);
    Value self = args[0];
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot mutate");
    }
    arrayPush(self, args[1]);
    return self;
}

// ex:
//   print a;
// OR
//   a.toString(); // => [1,2,3]
Value lxArrayToString(int argCount, Value *args) {
    CHECK_ARGS("Array#toString", 1, 1, argCount);
    Value self = *args;
    ASSERT(IS_AN_ARRAY(self));
    Obj* selfObj = AS_OBJ(self);
    Value ret = newStringInstance(copyString("[", 1));
    ObjString *bufRet = STRING_GETHIDDEN(ret);
    ValueArray *ary = ARRAY_GETHIDDEN(self);
    for (int i = 0; i < ary->count; i++) {
        Value elVal = ary->values[i];
        if (IS_OBJ(elVal) && (AS_OBJ(elVal) == selfObj)) {
            pushCString(bufRet, "[...]", 5);
            continue;
        }
        if (IS_OBJ(elVal)) {
            ASSERT(AS_OBJ(elVal)->type > OBJ_T_NONE);
        }
        ObjString *buf = valueToString(elVal, copyString);
        pushCString(bufRet, buf->chars, strlen(buf->chars));
        if (i < (ary->count-1)) {
            pushCString(bufRet, ",", 1);
        }
    }
    pushCString(bufRet, "]", 1);
    return ret;
}


Value lxArrayIndexGet(int argCount, Value *args) {
    CHECK_ARGS("Array#[]", 2, 2, argCount);
    Value self = args[0];
    ASSERT(IS_AN_ARRAY(self));
    Value num = args[1];
    CHECK_ARG_BUILTIN_TYPE(num, IS_NUMBER_FUNC, "number", 1);
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
    CHECK_ARG_BUILTIN_TYPE(num, IS_NUMBER_FUNC, "number", 1);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot mutate");
    }
    Value internalObjVal;
    ASSERT(tableGet(&selfObj->hiddenFields, OBJ_VAL(internedString("ary", 3)), &internalObjVal));
    ValueArray *ary = (ValueArray*)internalGetData(AS_INTERNAL(internalObjVal));
    ASSERT(ary);
    int idx = (int)AS_NUMBER(num);
    if (idx < 0) {
        // FIXME: throw error, or allow negative indices
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
            // FIXME: throw error
            ASSERT(IS_AN_ARRAY(el));
            if (ARRAY_SIZE(el) != 2) {
                fprintf(stderr, "Wrong array size given, expected 2\n");
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
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Map is frozen, cannot mutate");
    }
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
    setProp(self, internedString("message", 7), msg);
    return self;
}

static char fileReadBuf[4096];

Value lxFileReadStatic(int argCount, Value *args) {
    CHECK_ARGS("File.read", 2, 2, argCount);
    Value fname = args[1];
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    ObjString *fnameStr = VAL_TO_STRING(fname);
    if (!fileReadable(fnameStr->chars)) {
        if (errno == EACCES) {
            throwArgErrorFmt("File '%s' not readable", fnameStr->chars);
        } else {
            throwArgErrorFmt("File '%s' not found", fnameStr->chars);
        }
        return NIL_VAL;
    }
    FILE *f = fopen(fnameStr->chars, "r");
    if (!f) {
        throwArgErrorFmt("Error reading File '%s': %s", fnameStr->chars, strerror(errno));
        return NIL_VAL;
    }
    ObjString *retBuf = copyString("", 0);
    Value ret = newStringInstance(retBuf);
    size_t nread;
    while ((nread = fread(fileReadBuf, 1, sizeof(fileReadBuf), f)) > 0) {
        pushCString(retBuf, fileReadBuf, nread);
    }
    fclose(f);
    return ret;
}

bool runtimeCheckArgs(int min, int max, int actual) {
    return min <= actual && (max >= actual || max == -1);
}
