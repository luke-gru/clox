#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
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

void addGlobalFunction(const char *name, NativeFn func) {
    ObjString *funcName = internedString(name, strlen(name));
    ObjNative *natFn = newNative(funcName, func);
    tableSet(&vm.globals, OBJ_VAL(funcName), OBJ_VAL(natFn));
}

ObjClass *addGlobalClass(const char *name, ObjClass *super) {
    ObjString *className = internedString(name, strlen(name));
    ObjClass *objClass = newClass(className, super);
    tableSet(&vm.globals, OBJ_VAL(className), OBJ_VAL(objClass));
    return objClass;
}

ObjModule *addGlobalModule(const char *name) {
    ObjString *modName = internedString(name, strlen(name));
    ObjModule *mod = newModule(modName);
    tableSet(&vm.globals, OBJ_VAL(modName), OBJ_VAL(mod));
    return mod;
}

void addNativeMethod(void *klass, const char *name, NativeFn func) {
    ObjString *mname = internedString(name, strlen(name));
    ObjNative *natFn = newNative(mname, func);
    tableSet(&((ObjModule*)klass)->methods, OBJ_VAL(mname), OBJ_VAL(natFn));
}

void addNativeGetter(void *klass, const char *name, NativeFn func) {
    ObjString *mname = internedString(name, strlen(name));
    ObjNative *natFn = newNative(mname, func);
    tableSet(&((ObjModule*)klass)->getters, OBJ_VAL(mname), OBJ_VAL(natFn));
}

void addNativeSetter(void *klass, const char *name, NativeFn func) {
    ObjString *mname = internedString(name, strlen(name));
    ObjNative *natFn = newNative(mname, func);
    tableSet(&((ObjModule*)klass)->setters, OBJ_VAL(mname), OBJ_VAL(natFn));
}

// Does this file exist and is it readable?
static bool fileReadable(char *fname) {
    struct stat buffer;
    return (stat(fname, &buffer) == 0);
}

Value lxClock(int argCount, Value *args) {
    CHECK_ARITY("clock", 0, 0, argCount);
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

Value lxTypeof(int argCount, Value *args) {
    CHECK_ARITY("typeof", 1, 1, argCount);
    const char *strType = typeOfVal(*args);
    return newStringInstance(copyString(strType, strlen(strType)));
}

Value lxDebugger(int argCount, Value *args) {
    CHECK_ARITY("debugger", 0, 0, argCount);
    vm.debugger.awaitingPause = true;
    return NIL_VAL;
}

Value lxEval(int argCount, Value *args) {
    CHECK_ARITY("eval", 1, 1, argCount);
    Value src = *args;
    CHECK_ARG_IS_A(src, lxStringClass, 1);
    char *csrc = VAL_TO_STRING(src)->chars;
    if (strlen(csrc) == 0) {
        return NIL_VAL;
    }
    return VMEval(csrc, "(eval)", 1);
}

Value lxSleep(int argCount, Value *args) {
    CHECK_ARITY("sleep", 1, 1, argCount);
    Value nsecs = *args;
    CHECK_ARG_BUILTIN_TYPE(nsecs, IS_NUMBER_FUNC, "number", 1);
    int secs = (int)AS_NUMBER(nsecs);
    if (secs > 0) {
        releaseGVL();
        sleep(secs); // NOTE: could be interrupted by signal handler
        acquireGVL();
    }
    return NIL_VAL;
}

// Register atExit handler for process
Value lxAtExit(int argCount, Value *args) {
    CHECK_ARITY("atExit", 1, 1, argCount);
    Value func = *args;
    CHECK_ARG_BUILTIN_TYPE(func, IS_CLOSURE_FUNC, "function", 1);
    vec_push(&vm.exitHandlers, AS_OBJ(func));
    return NIL_VAL;
}

/**
 * Exit current thread
 * ex: exit(0);
 */
Value lxExit(int argCount, Value *args) {
    CHECK_ARITY("exit", 1, 1, argCount);
    Value status = *args;
    CHECK_ARG_BUILTIN_TYPE(status, IS_NUMBER_FUNC, "number", 1);
    stopVM((int)AS_NUMBER(status));
    return NIL_VAL; // not reached
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
    exitingThread();
    THREAD_DEBUG(2, "exiting new thread");
    releaseGVL();
    return AS_OBJ(pop());
}

Value lxNewThread(int argCount, Value *args) {
    CHECK_ARITY("newThread", 1, 1, argCount);
    Value closure = *args;
    CHECK_ARG_BUILTIN_TYPE(closure, IS_CLOSURE_FUNC, "function", 1);
    ObjClosure *func = AS_CLOSURE(closure);
    pthread_t tnew;
    if (pthread_create(&tnew, NULL, runCallableInNewThread, func) == 0) {
        THREAD_DEBUG(2, "created thread id %lu", (unsigned long)tnew);
        releaseGVL(); // allow thread to run if it's ready
        acquireGVL();
        return NUMBER_VAL((unsigned long)tnew);
    } else {
        // TODO: throw lxThreadErrClass
        throwErrorFmt(lxErrClass, "Error creating new thread");
    }
}

Value lxJoinThread(int argCount, Value *args) {
    CHECK_ARITY("joinThread", 1, 1, argCount);
    Value tidNum = *args;
    CHECK_ARG_BUILTIN_TYPE(tidNum, IS_NUMBER_FUNC, "number", 1);
    double num = AS_NUMBER(tidNum);
    THREAD_DEBUG(2, "Joining thread id %lu\n", (unsigned long)num);
    int ret = 0;
    releaseGVL();
    // blocking call until given thread ends execution
    if ((ret = pthread_join((pthread_t)num, NULL)) != 0) {
        THREAD_DEBUG(1, "Error joining thread: (ret=%d)", ret);
        // TODO: throw lxThreadErrClass
        throwErrorFmt(lxErrClass, "Error joining thread");
    }
    acquireGVL();
    return NIL_VAL;
}

Value lxThreadInit(int argCount, Value *args) {
    CHECK_ARITY("Thread#init", 1, 1, argCount);
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(NULL, sizeof(LxThread), NULL, NULL);
    LxThread *th = ALLOCATE(LxThread, 1); // GCed by default GC free of internalObject
    internalObj->data = th;
    tableSet(&selfObj->hiddenFields, OBJ_VAL(internedString("th", 2)), OBJ_VAL(internalObj));
    return self;
}

static Value loadScriptHelper(Value fname, bool checkLoaded) {
    char *cfile = VAL_TO_STRING(fname)->chars;
    bool isAbsFile = cfile[0] == pathSeparator;
    char pathbuf[300] = { '\0' };
    char curdir[250] = { '\0' };
    bool triedCurdir = false;
    bool fileFound = false;
    if (isAbsFile) {
        memcpy(pathbuf, cfile, strlen(cfile));
        fileFound = true;
    } else {
        Value el; int i = 0;
        LXARRAY_FOREACH(lxLoadPath, el, i) {
            if (!IS_A_STRING(el)) {
                fprintf(stderr, "Warning: non-string found in loadPath: type=%s\n", typeOfVal(el));
                continue;
            }
            char *dir = VAL_TO_STRING(el)->chars;
            memset(pathbuf, 0, 300);
            memcpy(pathbuf, dir, strlen(dir));
            if (strncmp(pathbuf, ".", 1) == 0) {
                if (!curdir[0] && !triedCurdir) {
                    char *cwdres = getcwd(curdir, 250);
                    triedCurdir = true;
                    if (cwdres == NULL) {
                        fprintf(stderr,
                                "Couldn't get current working directory for loading script!"
                                " Maybe too long?\n");
                        continue;
                    }
                }
                memcpy(pathbuf, curdir, strlen(curdir));
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
    if (!fileFound) {
        throwErrorFmt(lxLoadErrClass, "File '%s' not found", cfile);
    }
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
        vec_push(&vm.loadedScripts, newStringInstance(fpath));
    }
    InterpretResult ires = loadScript(&chunk, pathbuf);
    return BOOL_VAL(ires == INTERPRET_OK);
}

Value lxRequireScript(int argCount, Value *args) {
    CHECK_ARITY("requireScript", 1, 1, argCount);
    Value fname = *args;
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    return loadScriptHelper(fname, true);
}

Value lxLoadScript(int argCount, Value *args) {
    CHECK_ARITY("loadScript", 1, 1, argCount);
    Value fname = *args;
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    return loadScriptHelper(fname, false);
}

static void markInternalAry(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    ASSERT(internal);
    ValueArray *valAry = internal->data;
    ASSERT(valAry);
    for (int i = 0; i < valAry->count; i++) {
        Value val = valAry->values[i];
        if (!IS_OBJ(val)) continue;
        // XXX: this is needed for GC code not to segfault for some reason,
        // need to investigate. It especially happens after multiple (3) calls
        // to GC.collect().
        if (AS_OBJ(val)->type <= OBJ_T_INTERNAL) {
            blackenObject(AS_OBJ(val));
        }
    }
}

static void freeInternalAry(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    ASSERT(internal);
    ValueArray *valAry = internal->data;
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

// Creates a new object, with the same properties and hidden fields
// var o = Object(); var o2 = o.dup();
Value lxObjectDup(int argCount, Value *args) {
    CHECK_ARITY("Object#dup", 1, 1, argCount);
    Value self = *args;
    if (!IS_INSTANCE(self)) {
        // Must be a module or class
        throwErrorFmt(lxTypeErrClass, "Cannot duplicate (dup) a %s", typeOfVal(self));
    }
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInstance *newObj = newInstance(selfObj->klass);
    Entry e; int idx = 0;
    TABLE_FOREACH(&selfObj->fields, e, idx) {
        tableSet(&newObj->fields, e.key, e.value);
    }
    idx = 0;
    TABLE_FOREACH(&selfObj->hiddenFields, e, idx) {
        tableSet(&newObj->hiddenFields, e.key, e.value);
    }
    return OBJ_VAL(newObj);
}

// ex: var m = Module("MyMod");
Value lxModuleInit(int argCount, Value *args) {
    // TODO: call super?
    Value self = *args;
    CHECK_ARITY("Module#init", 1, 2, argCount);
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
    // TODO: call super?
    CHECK_ARITY("Class#init", 1, 3, argCount);
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
        UNREACHABLE_RETURN(vm.lastErrorThrown);
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
    CHECK_ARITY("Class#include", 2, 2, argCount);
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
    CHECK_ARITY("Class#name", 1, 1, argCount);
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

// ex: var a = Array();
//     var b = ["hi", 2, Map()];
Value lxArrayInit(int argCount, Value *args) {
    // TODO: call super?
    CHECK_ARITY("Array#init", 1, -1, argCount);
    Value self = *args;
    DBG_ASSERT(IS_AN_ARRAY(self));
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(NULL, 0, markInternalAry, freeInternalAry);
    ValueArray *ary = ALLOCATE(ValueArray, 1);
    initValueArray(ary);
    internalObj->data = ary;
    internalObj->dataSz = sizeof(ValueArray);
    tableSet(&selfObj->hiddenFields, OBJ_VAL(internedString("ary", 3)), OBJ_VAL(internalObj));
    for (int i = 1; i < argCount; i++) {
        writeValueArrayEnd(ary, args[i]);
    }
    ASSERT(ary->count == argCount-1);
    return self;
}

// ex: a.push(1);
Value lxArrayPush(int argCount, Value *args) {
    CHECK_ARITY("Array#push", 2, 2, argCount);
    Value self = args[0];
    arrayPush(self, args[1]);
    return self;
}

// Deletes last element in array and returns it.
// ex: var a = [1,2,3];
//     print a.pop(); => 3
//     print a; => [1,2]
Value lxArrayPop(int argCount, Value *args) {
    CHECK_ARITY("Array#pop", 1, 1, argCount);
    return arrayPop(*args);
}

// Adds an element to the beginning of the array and returns `self`
// ex: var a = [1,2,3];
//     a.pushFront(100);
//     print a; => [100, 1, 2, 3];
Value lxArrayPushFront(int argCount, Value *args) {
    CHECK_ARITY("Array#pushFront", 2, 2, argCount);
    Value self = args[0];
    arrayPushFront(self, args[1]);
    return self;
}

// Deletes an element from the beginning of the array and returns it.
// Returns nil if no elements left.
// ex: var a = [1,2,3];
//     print a.popFront(); => 1
//     print a; => [2,3]
Value lxArrayPopFront(int argCount, Value *args) {
    CHECK_ARITY("Array#popFront", 1, 1, argCount);
    return arrayPopFront(*args);
}

// ex: a.delete(2);
Value lxArrayDelete(int argCount, Value *args) {
    CHECK_ARITY("Array#delete", 2, 2, argCount);
    Value self = args[0];
    int idx = arrayDelete(self, args[1]);
    if (idx == -1) {
        return NIL_VAL;
    } else {
        return NUMBER_VAL(idx);
    }
}

// ex: a.clear();
Value lxArrayClear(int argCount, Value *args) {
    CHECK_ARITY("Array#clear", 1, 1, argCount);
    Value self = args[0];
    arrayClear(self);
    return self;
}

// ex:
//   print a;
// OR
//   a.toString(); // => [1,2,3]
Value lxArrayToString(int argCount, Value *args) {
    CHECK_ARITY("Array#toString", 1, 1, argCount);
    Value self = *args;
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
            DBG_ASSERT(AS_OBJ(elVal)->type > OBJ_T_NONE);
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


Value lxArrayOpIndexGet(int argCount, Value *args) {
    CHECK_ARITY("Array#[]", 2, 2, argCount);
    Value self = args[0];
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

Value lxArrayOpIndexSet(int argCount, Value *args) {
    CHECK_ARITY("Array#[]=", 3, 3, argCount);
    Value self = args[0];
    ObjInstance *selfObj = AS_INSTANCE(self);
    Value num = args[1];
    Value rval = args[2];
    CHECK_ARG_BUILTIN_TYPE(num, IS_NUMBER_FUNC, "number", 1);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    Value internalObjVal;
    ASSERT(tableGet(&selfObj->hiddenFields, OBJ_VAL(internedString("ary", 3)), &internalObjVal));
    ValueArray *ary = (ValueArray*)internalGetData(AS_INTERNAL(internalObjVal));
    ASSERT(ary);
    int idx = (int)AS_NUMBER(num);
    if (idx < 0) {
        // FIXME: throw error, or allow negative indices?
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

Value lxArrayIter(int argCount, Value *args) {
    CHECK_ARITY("Array#iter", 1, 1, argCount);
    return createIterator(*args);
}

Value lxArrayOpEquals(int argCount, Value *args) {
    CHECK_ARITY("Array#==", 2, 2, argCount);
    return BOOL_VAL(arrayEquals(args[0], args[1]));
}

typedef struct Iterator {
    int index; // # of times iterator was called with 'next' - 1
    int lastRealIndex; // for iterating over maps, not yet used
    ObjInstance *instance; // the array/map/instance we're iterating over
} Iterator;

static void markInternalIter(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    ASSERT(internal);
    ObjInstance *instance = ((Iterator*)internal->data)->instance;
    ASSERT(instance);
    blackenObject((Obj*)instance);
}

static void freeInternalIter(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    ASSERT(internal);
    ObjInstance *instance = ((Iterator*)internal->data)->instance;
    ASSERT(instance);
    unhideFromGC((Obj*)instance);
    freeObject((Obj*)instance, true); // release the actual memory
    FREE(Iterator, internal->data); // free the Iterator struct
}

Value lxIteratorInit(int argCount, Value *args) {
    CHECK_ARITY("Iterator#init", 2, 2, argCount);
    Value self = args[0];
    Value iterable = args[1];
    ObjInstance *selfObj = AS_INSTANCE(self);
    Iterator *iter = ALLOCATE(Iterator, 1);
    iter->index = -1;
    iter->lastRealIndex = -1;
    iter->instance = AS_INSTANCE(iterable);
    ObjInternal *internalIter = newInternalObject(
        iter, sizeof(Iterator), markInternalIter, freeInternalIter
    );
    tableSet(&selfObj->hiddenFields,
            OBJ_VAL(internedString("iter", 4)),
            OBJ_VAL(internalIter));
    return self;
}

Value lxIteratorNext(int argCount, Value *args) {
    CHECK_ARITY("Iterator#next", 1, 1, argCount);
    Value self = args[0];
    ObjInstance *selfObj = AS_INSTANCE(self);
    Value internalIter;
    ASSERT(tableGet(&selfObj->hiddenFields,
            OBJ_VAL(internedString("iter", 4)),
            &internalIter));
    ObjInternal *internalObj = AS_INTERNAL(internalIter);
    Iterator *iter = internalGetData(internalObj);
    ASSERT(iter);
    ObjInstance *iterableObj = iter->instance;
    Value iterable = OBJ_VAL(iterableObj);
    if (IS_AN_ARRAY(iterable)) {
        int nextIdx = ++(iter->index);
        if (nextIdx >= ARRAY_SIZE(iterable)) {
            return NIL_VAL;
        } else {
            Value ret = ARRAY_GET(iterable, nextIdx);
            ASSERT(!IS_UNDEF(ret));
            return ret;
        }
    } else if (IS_A_MAP(iterable)) {
        Table *map = MAP_GETHIDDEN(iterable);
        int nextIdx = ++(iter->index);
        if (nextIdx >= map->count) {
            return NIL_VAL;
        } else {
            int realIndex = -1;
            Entry e = tableNthEntry(map, nextIdx, &realIndex);
            if (realIndex >= 0) {
                Value ary = newArray();
                arrayPush(ary, e.key);
                arrayPush(ary, e.value);
                return ary;
            } else {
                return NIL_VAL; // shouldn't reach here
            }
        }
    } else {
        UNREACHABLE("bug"); // TODO: support other iterable types
    }
    UNREACHABLE(__func__);
}


Value lxErrInit(int argCount, Value *args) {
    // TODO: call super?
    CHECK_ARITY("Error#init", 1, 2, argCount);
    Value self = args[0];
    Value msg;
    if (argCount == 2) {
        msg = args[1];
    } else {
        msg = NIL_VAL;
    }
    setProp(self, internedString("message", 7), msg);
    return self;
}

Value lxGCStats(int argCount, Value *args) {
    CHECK_ARITY("GC.stats", 1, 1, argCount);
    Value map = newMap();
    Value bytesKey = newStringInstance(copyString("bytes", 5));
    mapSet(map, bytesKey, NUMBER_VAL(vm.bytesAllocated));
    return map;
}

Value lxGCCollect(int argCount, Value *args) {
    CHECK_ARITY("GC.collect", 1, 1, argCount);
    bool prevOn = turnGCOn();
    collectGarbage();
    setGCOnOff(prevOn);
    return NIL_VAL;
}

bool checkArity(int min, int max, int actual) {
    return min <= actual && (max >= actual || max == -1);
}

void checkBuiltinArgType(Value arg, value_type_p typechk_p, const char *typeExpect, int argnum) {
    if (!typechk_p(arg)) {
        const char *typeActual = typeOfVal(arg);
        throwArgErrorFmt("Expected argument %d to be a %s, got: %s", argnum, typeExpect, typeActual);
    }
}

void checkArgIsInstanceOf(Value arg, ObjClass *klass, int argnum) {
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

void checkArgIsA(Value arg, ObjClass *klass, int argnum) {
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
