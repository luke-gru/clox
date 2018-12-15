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

Value lxFork(int argCount, Value *args) {
    CHECK_ARGS("fork", 0, 1, argCount);
    Value func;
    if (argCount == 1) {
        func = *args;
        if (!isCallable(func)) {
            throwArgErrorFmt("Expected argument 1 to be callable, is: %s", typeOfVal(func));
            UNREACHABLE_RETURN(vm.lastErrorThrown);
        }
    }
    pid_t pid = fork();
    if (pid < 0) { // error, should throw?
        return NUMBER_VAL(-1);
    }
    if (pid) { // in parent
        return NUMBER_VAL(pid);
    } else { // in child
        if (argCount == 1) {
            callCallable(func, 0, false, NULL);
            stopVM(0);
        }
        return NIL_VAL;
    }
}

Value lxWaitpid(int argCount, Value *args) {
    CHECK_ARGS("waitpid", 1, 1, argCount);
    Value pidVal = *args;
    pid_t childpid = (pid_t)AS_NUMBER(pidVal);
    int wstatus;
    // TODO: allow wait flags
    releaseGVL();
    pid_t wret = waitpid(childpid, &wstatus, 0);
    acquireGVL();
    if (wret == -1) { // error, should throw?
        return NUMBER_VAL(-1);
    }
    return pidVal;
}

Value lxSleep(int argCount, Value *args) {
    CHECK_ARGS("sleep", 1, 1, argCount);
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

Value lxSystem(int argCount, Value *args) {
    CHECK_ARGS("system", 1, 1, argCount);
    Value cmd = *args;
    CHECK_ARG_IS_A(cmd, lxStringClass, 1);

    const char *cmdStr = VAL_TO_STRING(cmd)->chars;
    releaseGVL();
    int status = system(cmdStr);
    acquireGVL();
    int exitStatus = WEXITSTATUS(status);
    if (exitStatus != 0) {
        return BOOL_VAL(false);
    }
    return BOOL_VAL(true);
}

Value lxAtExit(int argCount, Value *args) {
    CHECK_ARGS("atExit", 1, 1, argCount);
    Value func = *args;
    CHECK_ARG_BUILTIN_TYPE(func, IS_CLOSURE_FUNC, "function", 1);
    vec_push(&vm.exitHandlers, AS_OBJ(func));
    return NIL_VAL;
}

/**
 * ex: exit(0);
 */
Value lxExit(int argCount, Value *args) {
    CHECK_ARGS("exit", 1, 1, argCount);
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
    CHECK_ARGS("newThread", 1, 1, argCount);
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
    int ret = 0;
    releaseGVL();
    // blocks
    if ((ret = pthread_join((pthread_t)num, NULL)) != 0) {
        // TODO: throw error
        THREAD_DEBUG(1, "Error joining thread: (ret=%d)", ret);
    }
    acquireGVL();
    return NIL_VAL;
}

Value lxThreadInit(int argCount, Value *args) {
    CHECK_ARGS("Thread#init", 1, 1, argCount);
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(NULL, sizeof(LxThread), NULL, NULL);
    LxThread *th = ALLOCATE(LxThread, 1); // GCed by default GC free of internalObject
    internalObj->data = th;
    tableSet(&selfObj->hiddenFields, OBJ_VAL(internedString("th", 2)), OBJ_VAL(internalObj));
    return self;
}

static Value loadScriptHelper(Value fname, const char *funcName, bool checkLoaded) {
    char *cfile = VAL_TO_STRING(fname)->chars;
    bool isAbsFile = cfile[0] == pathSeparator;
    char pathbuf[300] = { '\0' };
    bool fileFound = false;
    if (isAbsFile) {
        memcpy(pathbuf, cfile, strlen(cfile));
        fileFound = true;
    } else {
        Value el; int i = 0;
        LXARRAY_FOREACH(lxLoadPath, el, i) {
            if (!IS_A_STRING(el)) continue;
            char *dir = VAL_TO_STRING(el)->chars;
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
    CHECK_ARGS("Object#dup", 1, 1, argCount);
    Value self = *args;
    ASSERT(IS_INSTANCE(self)); // FIXME: what about dup for classes/modules?
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
    // TODO: call super?
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
    // TODO: call super?
    CHECK_ARGS("String#init", 1, 2, argCount);
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (argCount == 2) {
        Value internalStrVal = args[1];
        if (IS_T_STRING(internalStrVal)) { // string instance given, copy the buffer
            ObjString *orig = STRING_GETHIDDEN(internalStrVal);
            ObjString *new = dupString(orig);
            internalStrVal = OBJ_VAL(new);
        }
        if (!IS_STRING(internalStrVal)) { // other type given, convert to string
            ObjString *str = valueToString(internalStrVal, copyString);
            internalStrVal = OBJ_VAL(str);
        }
        ASSERT(IS_STRING(internalStrVal));
        tableSet(&selfObj->hiddenFields, OBJ_VAL(internedString("buf", 3)), internalStrVal);
    } else { // empty string
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
    ASSERT(IS_A_STRING(rhs)); // TODO: throw error or coerce into String
    ObjString *rhsBuf = STRING_GETHIDDEN(rhs);
    pushObjString(lhsBuf, rhsBuf);
    return ret;
}

// var s = "hey"; s.push(" there!"); => "hey there!"
Value lxStringPush(int argCount, Value *args) {
    CHECK_ARGS("String#push", 2, 2, argCount);
    Value self = *args;
    Value rhs = args[1];
    CHECK_ARG_IS_A(rhs, lxStringClass, 1);
    pushString(self, rhs);
    return self;
}

// ex: var s = "hey"; var s2 = s.dup(); s.push(" again");
//     print s;  => "hey"
//     print s2; => "hey again"
Value lxStringDup(int argCount, Value *args) {
    CHECK_ARGS("String#dup", 1, 1, argCount);
    Value ret = lxObjectDup(argCount, args);
    ObjInstance *retInst = AS_INSTANCE(ret);
    ObjString *buf = STRING_GETHIDDEN(ret);
    tableSet(&retInst->hiddenFields, OBJ_VAL(internedString("buf", 3)), OBJ_VAL(dupString(buf)));
    return ret;
}

// ex: var s = "going";
//     s.clear();
//     print s; => ""
Value lxStringClear(int argCount, Value *args) {
    CHECK_ARGS("String#clear", 1, 1, argCount);
    clearString(*args);
    return *args;
}

Value lxStringInsertAt(int argCount, Value *args) {
    CHECK_ARGS("String#insertAt", 3, 3, argCount);
    Value self = args[0];
    Value insert = args[1];
    Value at = args[2];
    // TODO: check types

    stringInsertAt(self, insert, (int)AS_NUMBER(at));
    return self;
}

Value lxStringSubstr(int argCount, Value *args) {
    CHECK_ARGS("String#substr", 3, 3, argCount);
    Value self = args[0];
    Value startIdx = args[1];
    Value len = args[2];
    CHECK_ARG_BUILTIN_TYPE(startIdx, IS_NUMBER_FUNC, "number", 2);
    CHECK_ARG_BUILTIN_TYPE(len, IS_NUMBER_FUNC, "number", 3);
    return stringSubstr(self, AS_NUMBER(startIdx), AS_NUMBER(len));
}

Value lxStringIndexGet(int argCount, Value *args) {
    CHECK_ARGS("String#indexGet", 2, 2, argCount);
    Value self = args[0];
    Value index = args[1];
    CHECK_ARG_BUILTIN_TYPE(index, IS_NUMBER_FUNC, "number", 2);
    return stringIndexGet(self, AS_NUMBER(index));
}

Value lxStringIndexSet(int argCount, Value *args) {
    CHECK_ARGS("String#indexSet", 3, 3, argCount);
    Value self = args[0];
    Value index = args[1];
    CHECK_ARG_BUILTIN_TYPE(index, IS_NUMBER_FUNC, "number", 2);
    Value chrStr = args[1];
    CHECK_ARG_IS_A(chrStr, lxStringClass, 3);
    char chr = VAL_TO_STRING(chrStr)->chars[0];
    stringIndexSet(self, AS_NUMBER(index), chr);
    return self;
}

// ex: var a = Array();
//     var b = ["hi", 2, Map()];
Value lxArrayInit(int argCount, Value *args) {
    // TODO: call super?
    CHECK_ARGS("Array#init", 1, -1, argCount);
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
    CHECK_ARGS("Array#push", 2, 2, argCount);
    Value self = args[0];
    arrayPush(self, args[1]);
    return self;
}

// Deletes last element in array and returns it.
// ex: var a = [1,2,3];
//     print a.pop(); => 3
//     print a; => [1,2]
Value lxArrayPop(int argCount, Value *args) {
    CHECK_ARGS("Array#pop", 1, 1, argCount);
    return arrayPop(*args);
}

// Adds an element to the beginning of the array and returns `self`
// ex: var a = [1,2,3];
//     a.pushFront(100);
//     print a; => [100, 1, 2, 3];
Value lxArrayPushFront(int argCount, Value *args) {
    CHECK_ARGS("Array#pushFront", 2, 2, argCount);
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
    CHECK_ARGS("Array#popFront", 1, 1, argCount);
    return arrayPopFront(*args);
}

// ex: a.delete(2);
Value lxArrayDelete(int argCount, Value *args) {
    CHECK_ARGS("Array#delete", 2, 2, argCount);
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
    CHECK_ARGS("Array#clear", 1, 1, argCount);
    Value self = args[0];
    arrayClear(self);
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
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
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

Value lxArrayIter(int argCount, Value *args) {
    CHECK_ARGS("Array#iter", 1, 1, argCount);
    return createIterator(*args);
}

Value lxArrayOpEquals(int argCount, Value *args) {
    CHECK_ARGS("Array#opEquals", 2, 2, argCount);
    return BOOL_VAL(arrayEquals(args[0], args[1]));
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
    // TODO: call super?
    CHECK_ARGS("Map#init", 1, -1, argCount);
    Value self = args[0];
    ASSERT(IS_A_MAP(self));
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
                UNREACHABLE_RETURN(vm.lastErrorThrown);
            }
            if (ARRAY_SIZE(el) != 2) {
                throwArgErrorFmt("Wrong array size given, expected 2, got: %d",
                        ARRAY_SIZE(el));
                UNREACHABLE_RETURN(vm.lastErrorThrown);
            }
            Value mapKey = ARRAY_GET(el, 0);
            Value mapVal = ARRAY_GET(el, 1);
            tableSet(map, mapKey, mapVal);
        }
    } else {
        throwArgErrorFmt("Expected 1 argument, got %d", argCount-1);
        UNREACHABLE_RETURN(vm.lastErrorThrown);
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
        throwErrorFmt(lxErrClass, "%s", "Map is frozen, cannot modify");
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

Value lxMapIter(int argCount, Value *args) {
    CHECK_ARGS("Map#iter", 1, 1, argCount);
    return createIterator(*args);
}

Value lxMapOpEquals(int argCount, Value *args) {
    CHECK_ARGS("Map#opEquals", 2, 2, argCount);
    return BOOL_VAL(mapEquals(args[0], args[1]));
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
    CHECK_ARGS("Iterator#init", 2, 2, argCount);
    Value self = args[0];
    Value iterable = args[1];
    ObjInstance *selfObj = AS_INSTANCE(self);
    Iterator *iter = ALLOCATE(Iterator, 1);
    ASSERT_MEM(iter);
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
    CHECK_ARGS("Iterator#next", 1, 1, argCount);
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

Value lxMapClear(int argCount, Value *args) {
    CHECK_ARGS("Map#clear", 1, 1, argCount);
    Value self = args[0];
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Map is frozen, cannot modify");
    }
    mapClear(self);
    return self;
}

Value lxErrInit(int argCount, Value *args) {
    // TODO: call super?
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
        UNREACHABLE_RETURN(vm.lastErrorThrown);
    }
    // TODO: release and re-acquire GVL for fopen, or is it fast enough?
    FILE *f = fopen(fnameStr->chars, "r");
    if (!f) {
        throwArgErrorFmt("Error reading File '%s': %s", fnameStr->chars, strerror(errno));
        UNREACHABLE_RETURN(vm.lastErrorThrown);
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

Value lxGCStats(int argCount, Value *args) {
    CHECK_ARGS("GC.stats", 1, 1, argCount);
    Value map = newMap();
    Value bytesKey = newStringInstance(copyString("bytes", 5));
    mapSet(map, bytesKey, NUMBER_VAL(vm.bytesAllocated));
    return map;
}

Value lxGCCollect(int argCount, Value *args) {
    CHECK_ARGS("GC.collect", 1, 1, argCount);
    bool prevOn = turnGCOn();
    collectGarbage();
    setGCOnOff(prevOn);
    return NIL_VAL;
}

bool runtimeCheckArgs(int min, int max, int actual) {
    return min <= actual && (max >= actual || max == -1);
}
