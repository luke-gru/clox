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
#include <limits.h>
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
    ObjString *funcName = INTERNED(name, strlen(name));
    ObjNative *natFn = newNative(funcName, func, NEWOBJ_FLAG_OLD);
    hideFromGC((Obj*)natFn);
    ASSERT(tableSet(&vm.globals, OBJ_VAL(funcName), OBJ_VAL(natFn)));
    unhideFromGC((Obj*)natFn);
}

ObjClass *addGlobalClass(const char *name, ObjClass *super) {
    ObjString *className = INTERNED(name, strlen(name));
    ObjClass *objClass = newClass(className, super, NEWOBJ_FLAG_OLD);
    hideFromGC((Obj*)objClass);
    ASSERT(tableSet(&vm.globals, OBJ_VAL(className), OBJ_VAL(objClass)));
    unhideFromGC((Obj*)objClass);
    return objClass;
}

ObjModule *addGlobalModule(const char *name) {
    ObjString *modName = INTERNED(name, strlen(name));
    ObjModule *mod = newModule(modName, NEWOBJ_FLAG_OLD);
    hideFromGC((Obj*)mod);
    ASSERT(tableSet(&vm.globals, OBJ_VAL(modName), OBJ_VAL(mod)));
    unhideFromGC((Obj*)mod);
    return mod;
}

ObjNative *addNativeMethod(void *klass, const char *name, NativeFn func) {
    DBG_ASSERT(klass);
    ObjString *mname = INTERNED(name, strlen(name));
    ObjNative *natFn = newNative(mname, func, NEWOBJ_FLAG_OLD);
    OBJ_WRITE(OBJ_VAL(klass), OBJ_VAL(natFn));
    natFn->klass = (Obj*)klass; // class or module
    natFn->isStatic = false;
    if (klass && natFn->klass->type == OBJ_T_CLASS) {
        natFn->isStatic = CLASSINFO(klass)->singletonOf != NULL;
    }
    hideFromGC((Obj*)natFn);
    ASSERT(tableSet(CLASSINFO(klass)->methods, OBJ_VAL(mname), OBJ_VAL(natFn)));
    unhideFromGC((Obj*)natFn);
    return natFn;
}

ObjNative *addNativeGetter(void *klass, const char *name, NativeFn func) {
    DBG_ASSERT(klass);
    ObjString *mname = INTERNED(name, strlen(name));
    ObjNative *natFn = newNative(mname, func, NEWOBJ_FLAG_OLD);
    OBJ_WRITE(OBJ_VAL(klass), OBJ_VAL(natFn));
    natFn->klass = (Obj*)klass; // class or module
    hideFromGC((Obj*)natFn);
    ASSERT(tableSet(CLASSINFO(klass)->getters, OBJ_VAL(mname), OBJ_VAL(natFn)));
    unhideFromGC((Obj*)natFn);
    return natFn;
}

ObjNative *addNativeSetter(void *klass, const char *name, NativeFn func) {
    DBG_ASSERT(klass);
    ObjString *mname = INTERNED(name, strlen(name));
    ObjNative *natFn = newNative(mname, func, NEWOBJ_FLAG_OLD);
    OBJ_WRITE(OBJ_VAL(klass), OBJ_VAL(natFn));
    natFn->klass = (Obj*)klass; // class or module
    hideFromGC((Obj*)natFn);
    ASSERT(tableSet(CLASSINFO(klass)->setters, OBJ_VAL(mname), OBJ_VAL(natFn)));
    unhideFromGC((Obj*)natFn);
    return natFn;
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
    return OBJ_VAL(copyString(strType, strlen(strType), NEWOBJ_FLAG_NONE));
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

static void threadSleep(LxThread *th, int secs) {
    pthread_mutex_lock(&th->sleepMutex);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += secs;
    THREAD_DEBUG(1, "Sleeping %lu\n", pthread_self());
    th->status = THREAD_SLEEPING;
    int res = pthread_cond_timedwait(&th->sleepCond, &th->sleepMutex, &ts);
    th->status = THREAD_STOPPED;
    if (res == 0) {
        THREAD_DEBUG(1, "Woke up %lu\n", pthread_self());
    } else {
        THREAD_DEBUG(1, "Woke up with error: (err=%d) %lu\n", res, pthread_self());
    }
    pthread_mutex_unlock(&th->sleepMutex);
}

void threadSleepNano(LxThread *th, int nsecs) {
    pthread_mutex_lock(&th->sleepMutex);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += nsecs;
    THREAD_DEBUG(1, "Sleeping %lu\n", pthread_self());
    th->status = THREAD_SLEEPING;
    int res = pthread_cond_timedwait(&th->sleepCond, &th->sleepMutex, &ts);
    th->status = THREAD_STOPPED;
    if (res == 0) {
        THREAD_DEBUG(1, "Woke up %lu\n", pthread_self());
    } else {
        /*THREAD_DEBUG(1, "Woke up with error: (err=%d,errno=%s) %lu\n", res, strerror(res), pthread_self());*/
    }
    pthread_mutex_unlock(&th->sleepMutex);
}

Value lxSleep(int argCount, Value *args) {
    CHECK_ARITY("sleep", 1, 1, argCount);
    Value nsecs = *args;
    CHECK_ARG_BUILTIN_TYPE(nsecs, IS_NUMBER_FUNC, "number", 1);
    int secs = (int)AS_NUMBER(nsecs);
    if (secs > 0) {
        LxThread *th = THREAD();
        releaseGVL(THREAD_STOPPED);
        threadSleep(th, secs);
        acquireGVL();
    }
    return NIL_VAL;
}

static inline ObjClosure *closureFromFn(ObjFunction *func) {
    return newClosure(func, NEWOBJ_FLAG_NONE);
}

static inline CallFrame *getOuterClosureFrame() {
    CallFrame *frame = getFrame()->prev;
    while (frame) {
        if (!frame->closure) {
            frame = frame->prev;
            continue;
        }
        return frame;
    }
    return NULL;
}

static void fillClosureUpvalues(ObjClosure *block, ObjClosure *outer, CallFrame *frame) {
    ObjFunction *blockFn = block->function;
    ASSERT(blockFn);
    ASSERT(frame);
    for (int i = 0; i < blockFn->upvalueCount; i++) {
        uint8_t index = blockFn->upvaluesInfo[i].index;
        if (blockFn->upvaluesInfo[i].isLocal) {
            block->upvalues[i] = captureUpvalue(frame->slots+index);
        } else {
            DBG_ASSERT(outer->upvalues);
            block->upvalues[i] = outer->upvalues[index];
            DBG_ASSERT(block->upvalues[i]);
        }
    }
}

static ObjClosure *getCFrameBlockClosure() {
    BlockStackEntry *bentry = vec_last_or(&THREAD()->v_blockStack, NULL);
    Obj *block = NULL;
    if (bentry) {
        block = bentry->callable;
    }
    if (!block) {
        throwErrorFmt(lxErrClass, "Cannot yield, no block given");
    }
    if (bentry->cachedBlockClosure) {
        return bentry->cachedBlockClosure;
    }
    ObjClosure *blockClosure = closureFromFn((ObjFunction*)block);
    blockClosure->isBlock = true;
    CallFrame *frame = getOuterClosureFrame();
    ASSERT(frame);
    ObjClosure *lastClosure = frame->closure;
    ASSERT(lastClosure);
    fillClosureUpvalues(blockClosure, lastClosure, frame);
    bentry->cachedBlockClosure = blockClosure;
    return blockClosure;
}

Value lxYield(int argCount, Value *args) {
    CallFrame *frame = getFrame()->prev;
    ASSERT(frame->callInfo);
    Obj *block = (Obj*)frame->callInfo->blockFunction;
    if (!block && !frame->callInfo->blockInstance) {
        throwErrorFmt(lxErrClass, "Cannot yield, no block given");
    }
    Value callable = NIL_VAL;
    if (block) {
        DBG_ASSERT(IS_FUNCTION(OBJ_VAL(block)));
        ObjClosure *blockClosure = NULL;
        blockClosure = closureFromFn((ObjFunction*)block);
        blockClosure->isBlock = true;
        CallFrame *outerFrame = getOuterClosureFrame();
        ObjClosure *outerClosure = outerFrame->closure;
        ASSERT(outerClosure);
        fillClosureUpvalues(blockClosure, outerClosure, outerFrame);
        callable = OBJ_VAL(blockClosure);
        push(callable);
    } else if (frame->callInfo->blockInstance) {
        ObjInstance *blockInst = frame->callInfo->blockInstance;
        Obj *blkCallable = blockCallable(OBJ_VAL(blockInst));
        if (blkCallable->type == OBJ_T_CLOSURE) {
            block = blkCallable;
        }
        callable = OBJ_VAL(blkCallable);
        push(callable);
    }
    for (int i = 0; i < argCount; i++) {
        push(args[i]);
    }
    CallInfo cinfo = {
        .argc = argCount,
        .blockFunction = NULL,
        .isYield = true, // tell callCallable to adjust frame stack in popFrame()
        .blockIterFunc = NULL,
    };
    volatile int status = 0;
    volatile LxThread *th = THREAD();
    volatile BlockStackEntry *bentry = NULL;
    if (block) {
        SETUP_BLOCK(block, bentry, status, th->errInfo)
            if (status == TAG_NONE) {
            } else if (status == TAG_RAISE) {
                ObjInstance *errInst = AS_INSTANCE(THREAD()->lastErrorThrown);
                ASSERT(errInst);
                if (errInst->klass == lxBreakBlockErrClass) {
                    return NIL_VAL;
                } else if (errInst->klass == lxContinueBlockErrClass) { // continue
                    return getProp(THREAD()->lastErrorThrown, INTERN("ret"));
                } else if (errInst->klass == lxReturnBlockErrClass) {
                    return getProp(THREAD()->lastErrorThrown, INTERN("ret"));
                } else {
                    throwError(th->lastErrorThrown);
                }
            }
    }
    callCallable(callable, argCount, false, &cinfo);
    return pop(); // yielded a native function
}

Value yieldFromC(int argCount, Value *args, ObjInstance *blockObj) {
    Value callable;
    if (blockObj) {
        Obj *blkCallable = NULL;
        blkCallable = blockCallable(OBJ_VAL(blockObj));
        callable = OBJ_VAL(blkCallable);
    } else {
        ObjClosure *blkClosure = NULL;
        blkClosure = getCFrameBlockClosure();
        callable = OBJ_VAL(blkClosure);
    }
    push(callable);
    for (int i = 0; i < argCount; i++) {
        push(args[i]);
    }
    CallInfo *cinfoIn = getFrame()->callInfo;
    int blockExtraParamsNum = cinfoIn ? cinfoIn->blockArgsNumExtra : 0;
    for (int i = 0; i < blockExtraParamsNum; i++) {
        push(cinfoIn->blockArgsExtra[i]);
        argCount++;
    }
    CallInfo cinfo = {
        .argc = argCount,
        .blockFunction = NULL,
        .isYield = true, // tell callCallable to adjust frame stack in popFrame()
        .blockIterFunc = NULL,
        .blockInstance = blockObj,
    };
    callCallable(callable, argCount, false, &cinfo);
    return pop(); // if got here, was a native function
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
    UNREACHABLE_RETURN(NIL_VAL);
}

#define SCRIPT_PATH_MAX (PATH_MAX+1)
#define SCRIPT_DIR_MAX (PATH_MAX+1-100)
static Value loadScriptHelper(Value fname, bool checkLoaded) {
    char *cfile = VAL_TO_STRING(fname)->chars;
    bool isAbsFile = cfile[0] == pathSeparator;
    char pathbuf[SCRIPT_PATH_MAX] = { '\0' };
    char curdir[SCRIPT_DIR_MAX] = { '\0' };
    bool triedCurdir = false;
    bool fileFound = false;
    if (isAbsFile) {
        memcpy(pathbuf, cfile, strlen(cfile));
        fileFound = true;
    } else {
        Value el; int i = 0;
        LXARRAY_FOREACH(OBJ_VAL(lxLoadPath), el, i) {
            if (!IS_STRING(el)) {
                fprintf(stderr, "Warning: non-string found in loadPath: type=%s\n", typeOfVal(el));
                continue;
            }
            char *dir = VAL_TO_STRING(el)->chars;
            memset(pathbuf, 0, SCRIPT_PATH_MAX);
            memcpy(pathbuf, dir, strlen(dir));
            if (strncmp(pathbuf, ".", 1) == 0) { // "./path/to/file.lox"
                if (!curdir[0] && !triedCurdir) {
                    char *cwdres = getcwd(curdir, SCRIPT_DIR_MAX);
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
            bool isReadable = false;
readableCheck:
            isReadable = fileReadable(pathbuf);
            if (!isReadable) {
                if (!strstr(pathbuf, ".lox") && strlen(pathbuf)+4 < SCRIPT_PATH_MAX) {
                    strcat(pathbuf, ".lox");
                    goto readableCheck;
                }
                continue; // look in other directories for file
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
    CompileErr err = COMPILE_ERR_NONE;
    Chunk *chunk = compile_file(pathbuf, &err);
    if (!chunk || err != COMPILE_ERR_NONE) {
        freeChunk(chunk);
        FREE(Chunk, chunk);
        // TODO: throw syntax error
        return BOOL_VAL(false);
    }
    ObjString *fpath = copyString(pathbuf, strlen(pathbuf), NEWOBJ_FLAG_OLD);
    if (checkLoaded) {
        vec_push(&vm.loadedScripts, OBJ_VAL(fpath));
    }
    InterpretResult ires = loadScript(chunk, pathbuf);
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

Value lxObjectInit(int argCount, Value *args) {
    CHECK_ARITY("Object#init", 1, 1, argCount);
    return *args;
}

Value lxObjectFreeze(int argCount, Value *args) {
    CHECK_ARITY("Object#freeze", 1, 1, argCount);
    Obj *obj = AS_OBJ(*args);
    objFreeze(obj);
    return *args;
}

Value lxObjectUnfreeze(int argCount, Value *args) {
    CHECK_ARITY("Object#unfreeze", 1, 1, argCount);
    Obj *obj = AS_OBJ(*args);
    objUnfreeze(obj);
    return *args;
}

Value lxObjectIsFrozen(int argCount, Value *args) {
    CHECK_ARITY("Object#isFrozen", 1, 1, argCount);
    Obj *obj = AS_OBJ(*args);
    return BOOL_VAL(isFrozen(obj));
}

// ex: var o = Object(); print o.class;
Value lxObjectGetClass(int argCount, Value *args) {
    Value self = *args;
    ObjClass *klass = AS_INSTANCE(self)->klass;
    if (klass) {
        return OBJ_VAL(klass);
    } else {
        return NIL_VAL;
    }
}

Value lxObjectGetSingletonClass(int argCount, Value *args) {
    Value self = *args;
    return OBJ_VAL(singletonClass(AS_OBJ(self)));
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
    if (UNLIKELY(!IS_INSTANCE(self) && !IS_ARRAY(self) && !IS_STRING(self) && !IS_MAP(self))) {
        // Must be a module or class
        throwErrorFmt(lxTypeErrClass, "Cannot call dup() on a %s", typeOfVal(self));
    }
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInstance *newObj = newInstance(selfObj->klass, NEWOBJ_FLAG_NONE); // XXX: Call initialize on new instance?
    Entry e; int idx = 0;
    TABLE_FOREACH(selfObj->fields, e, idx, {
        tableSet(newObj->fields, e.key, e.value);
        OBJ_WRITE(OBJ_VAL(newObj), e.key);
        OBJ_WRITE(OBJ_VAL(newObj), e.value);
    })
    return OBJ_VAL(newObj);
}

Value lxObjectExtend(int argCount, Value *args) {
    CHECK_ARITY("Object#extend", 2, 2, argCount);
    Value self = *args;
    Obj *obj = AS_OBJ(self);
    ObjClass *klass = singletonClass(obj);
    Value includeArgs[2];
    includeArgs[0] = OBJ_VAL(klass);
    includeArgs[1] = args[1];
    lxClassInclude(2, includeArgs); // TODO: call method
    return self;
}

Value lxObjectHashKey(int argCount, Value *args) {
    CHECK_ARITY("Object#hashKey", 1, 1, argCount);
    Value self = *args;
    char buf[20] = {'\0'};
    sprintf(buf, "%p", AS_OBJ(self));
    return NUMBER_VAL(hashString(buf, strlen(buf))); // hash the pointer string of Obj*
}

Value lxObjectOpEquals(int argCount, Value *args) {
    CHECK_ARITY("Object#opEquals", 2, 2, argCount);
    return BOOL_VAL(AS_OBJ(args[0]) == AS_OBJ(args[1])); // pointer equality
}

Value lxObjectSend(int argCount, Value *args) {
    CHECK_ARITY("Object#send", 2, -1, argCount);
    Value self = *args;
    Value mnameVal = args[1];
    CHECK_ARG_IS_A(mnameVal, lxStringClass, 1);
    ObjString *mname = AS_STRING(mnameVal);
    return callMethod(AS_OBJ(self), mname, argCount-2, args+2, getFrame()->callInfo);
}

// ex: var m = Module("MyMod");
Value lxModuleInit(int argCount, Value *args) {
    CHECK_ARITY("Module#init", 1, 2, argCount);
    Value self = *args;
    ASSERT(!IS_INSTANCE(self));
    callSuper(0, NULL, NULL);
    if (argCount == 1) { return self; } // anonymous (unnamed) module
    Value name = args[1];
    CHECK_ARG_IS_A(name, lxStringClass, 1);
    ObjString *nameStr = AS_STRING(name);
    ObjModule *mod = AS_MODULE(self);
    ASSERT(CLASSINFO(mod)->name == NULL);
    ObjString *nameCpy = dupString(nameStr);
    CLASSINFO(mod)->name = nameCpy;
    return self;
}

// ex: var c = Class("MyClass", Object);
Value lxClassInit(int argCount, Value *args) {
    CHECK_ARITY("Class#init", 1, 3, argCount);
    Value self = *args;
    ASSERT(!IS_INSTANCE(self));
    callSuper(0, NULL, NULL);
    ObjClass *klass = AS_CLASS(self);
    if (argCount == 1) {
        return self;
    }
    Value arg1 = args[1]; // name or superclass
    ObjString *name = NULL;
    ObjClass *superClass = NULL;
    if (IS_STRING(arg1)) {
        name = dupString(VAL_TO_STRING(arg1));
    } else if (IS_CLASS(arg1)) {
        superClass = AS_CLASS(arg1);
    } else {
        throwArgErrorFmt("Expected argument 1 to be String or Class, got: %s", typeOfVal(arg1));
    }
    if (argCount == 3 && !superClass) {
        CHECK_ARG_IS_INSTANCE_OF(args[2], lxClassClass, 2);
        superClass = AS_CLASS(args[2]);
    }
    CLASSINFO(klass)->name = name;
    CLASSINFO(klass)->superclass = (Obj*)superClass;
    if (superClass) {
        OBJ_WRITE(OBJ_VAL(klass), OBJ_VAL(superClass));
    }
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
    vec_find(&CLASSINFO(klass)->v_includedMods, mod, alreadyIncluded);
    OBJ_WRITE(OBJ_VAL(klass), OBJ_VAL(mod));
    if (alreadyIncluded == -1) {
        vec_push(&CLASSINFO(klass)->v_includedMods, mod);
        ObjIClass *iclass = newIClass(klass, mod, NEWOBJ_FLAG_OLD);
        OBJ_WRITE(OBJ_VAL(klass), OBJ_VAL(iclass));
        setupIClass(iclass);
    }
    return modVal;
}

// Returns a copy of the class's name as a String
// ex: print Object.name
Value lxClassGetName(int argCount, Value *args) {
    CHECK_ARITY("Class#name", 1, 1, argCount);
    Value self = args[0];
    ObjClass *klass = AS_CLASS(self);
    ObjString *origName = CLASSINFO(klass)->name;
    if (origName == NULL) {
        return OBJ_VAL(copyString("(anon)", 6, NEWOBJ_FLAG_NONE));
    } else {
        return OBJ_VAL(dupString(origName));
    }
}

// ex: print Object._superClass;
Value lxClassGetSuperclass(int argCount, Value *args) {
    Value self = *args;
    Obj *klass = AS_OBJ(self);
    Obj *superClass;
    while ((superClass = CLASS_SUPER(klass))) {
        if (superClass->type == OBJ_T_CLASS) { // found
            break;
        } else { // iclass (module)
            klass = CLASS_SUPER(superClass);
        }
    }
    if (superClass) {
        return OBJ_VAL(superClass);
    } else {
        return NIL_VAL;
    }
}

#define FLAG_ITER_ARRAY 1
#define FLAG_ITER_MAP 2
#define FLAG_ITER_INSTANCE 4

typedef struct Iterator {
    int index; // # of times iterator was called with 'next' - 1
    int lastRealIndex; // for iterating over maps, not yet used
    ObjInstance *instance; // the array/map/instance we're iterating over
    int flags;
} Iterator;

static void markInternalIter(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    DBG_ASSERT(internal);
    ObjInstance *instance = ((Iterator*)internal->data)->instance;
    DBG_ASSERT(instance);
    grayObject((Obj*)instance);
}

static void freeInternalIter(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    DBG_ASSERT(internal);
    FREE(Iterator, internal->data); // free the Iterator struct
}

Value lxIteratorInit(int argCount, Value *args) {
    CHECK_ARITY("Iterator#init", 2, 2, argCount);
    callSuper(0, NULL, NULL);
    Value self = args[0];
    Value iterable = args[1];
    ObjInstance *selfObj = AS_INSTANCE(self);
    Iterator *iter = ALLOCATE(Iterator, 1);
    iter->index = -1;
    iter->lastRealIndex = -1;
    iter->instance = AS_INSTANCE(iterable);
    OBJ_WRITE(self, iterable);
    iter->flags = 0;
    if (IS_AN_ARRAY(iterable)) {
        iter->flags |= FLAG_ITER_ARRAY;
    } else if (IS_A_MAP(iterable)) {
        iter->flags |= FLAG_ITER_MAP;
    } else if (IS_INSTANCE(iterable)) {
        iter->flags |= FLAG_ITER_INSTANCE;
    } else {
        throwErrorFmt(lxTypeErrClass, "Expected Array/Map/Instance");
    }
    ObjInternal *internalIter = newInternalObject(false,
        iter, sizeof(Iterator), markInternalIter, freeInternalIter,
        NEWOBJ_FLAG_NONE
    );
    selfObj->internal = internalIter;
    return self;
}

Value lxIteratorNext(int argCount, Value *args) {
    CHECK_ARITY("Iterator#next", 1, 1, argCount);
    Value self = args[0];
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = selfObj->internal;
    Iterator *iter = internalGetData(internalObj);
    DBG_ASSERT(iter);
    ObjInstance *iterableObj = iter->instance;
    Value iterable = OBJ_VAL(iterableObj);
    if (iter->flags & FLAG_ITER_ARRAY) {
        int nextIdx = ++(iter->index);
        if (nextIdx >= ARRAY_SIZE(iterable)) {
            return NIL_VAL;
        } else {
            Value ret = ARRAY_GET(iterable, nextIdx);
            ASSERT(!IS_UNDEF(ret));
            return ret;
        }
    } else if (iter->flags & FLAG_ITER_MAP) {
        Table *map = AS_MAP(iterable)->table;
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
    } else if (iter->flags & FLAG_ITER_INSTANCE) {
        return callMethod(AS_OBJ(iterable), INTERN("iterNext"), 0, NULL, NULL);
    } else {
        UNREACHABLE("bug, typeof val: %s", typeOfVal(iterable));
    }
    UNREACHABLE(__func__);
}


Value lxErrInit(int argCount, Value *args) {
    CHECK_ARITY("Error#init", 1, 2, argCount);
    callSuper(0, NULL, NULL);
    Value self = args[0];
    Value msg;
    if (argCount == 2) {
        msg = args[1];
        if (LIKELY(!IS_NIL(msg))) {
            CHECK_ARG_IS_A(msg, lxStringClass, 1);
        }
    } else {
        msg = NIL_VAL;
    }
    setProp(self, INTERNED("message", 7), msg);
    return self;
}

Value lxGCStats(int argCount, Value *args) {
    CHECK_ARITY("GC.stats", 1, 1, argCount);
    Value map = newMap();
    Value totalKey = OBJ_VAL(copyString("totalAllocated", 14, NEWOBJ_FLAG_NONE));
    mapSet(map, totalKey, NUMBER_VAL(GCStats.totalAllocated));
    Value heapSizeKey = OBJ_VAL(copyString("heapSize", 8, NEWOBJ_FLAG_NONE));
    mapSet(map, heapSizeKey, NUMBER_VAL(GCStats.heapSize));
    Value heapUsedKey = OBJ_VAL(copyString("heapUsed", 8, NEWOBJ_FLAG_NONE));
    mapSet(map, heapUsedKey, NUMBER_VAL(GCStats.heapUsed));
    Value heapWasteKey = OBJ_VAL(copyString("heapUsedWaste", 13, NEWOBJ_FLAG_NONE));
    mapSet(map, heapWasteKey, NUMBER_VAL(GCStats.heapUsedWaste));
    Value runsYoungKey = OBJ_VAL(copyString("runsYoung", 9, NEWOBJ_FLAG_NONE));
    mapSet(map, runsYoungKey, NUMBER_VAL(GCProf.runsYoung));
    Value runsFullKey = OBJ_VAL(copyString("runsFull", 8, NEWOBJ_FLAG_NONE));
    mapSet(map, runsFullKey, NUMBER_VAL(GCProf.runsFull));
    return map;
}

Value lxGCCollect(int argCount, Value *args) {
    CHECK_ARITY("GC.collect", 1, 1, argCount);
    bool prevOn = turnGCOn();
    collectGarbage();
    setGCOnOff(prevOn);
    return NIL_VAL;
}

Value lxGCCollectYoung(int argCount, Value *args) {
    CHECK_ARITY("GC.collectYoung", 1, 1, argCount);
    bool prevOn = turnGCOn();
    collectYoungGarbage();
    setGCOnOff(prevOn);
    return NIL_VAL;
}

Value lxGCOff(int argCount, Value *args) {
    bool prevOn = turnGCOff();
    return BOOL_VAL(prevOn);
}

Value lxGCOn(int argCount, Value *args) {
    bool prevOn = turnGCOn();
    return BOOL_VAL(prevOn);
}

Value lxGCSetFinalizer(int argCount, Value *args) {
    CHECK_ARITY("GC.setFinalizer", 3, 3, argCount);
    Value objVal = args[1];
    if (!IS_INSTANCE_LIKE(objVal)) {
        throwErrorFmt(lxErrClass, "Finalizer can only be set on instances");
    }
    Value callable = args[2];
    if (!isCallable(callable)) {
        throwErrorFmt(lxErrClass, "Finalizer must be a callable");
    }
    setObjectFinalizer(AS_INSTANCE(objVal), AS_OBJ(callable));
    ASSERT(AS_INSTANCE(objVal)->finalizerFunc == AS_OBJ(callable));
    return NIL_VAL;
}

ObjClass *sysErrClass(int err) {
    (void)err;
    return lxSystemErrClass;
}

bool checkArity(int min, int max, int actual) {
    return min <= actual && (max >= actual || max == -1);
}

void checkBuiltinArgType(Value arg, value_type_p typechk_p, const char *typeExpect, int argnum) {
    if (UNLIKELY(!typechk_p(arg))) {
        const char *typeActual = typeOfVal(arg);
        throwArgErrorFmt("Expected argument %d to be a %s, got: %s", argnum, typeExpect, typeActual);
    }
}

void checkArgIsInstanceOf(Value arg, ObjClass *klass, int argnum) {
    if (UNLIKELY(!is_value_instance_of_p(arg, klass))) {
        const char *typeExpect = CLASSINFO(klass)->name->chars;
        const char *typeActual = NULL;
        if (IS_INSTANCE(arg)) {
            typeActual = className(klass);
        } else {
            typeActual = typeOfVal(arg);
        }
        throwArgErrorFmt("Expected argument %d to be of exact class %s, got: %s", argnum, typeExpect, typeActual);
    }
}

void checkArgIsA(Value arg, ObjClass *klass, int argnum) {
    if (UNLIKELY(!is_value_a_p(arg, klass))) {
        const char *typeExpect = className(klass);
        const char *typeActual = NULL;
        if (IS_INSTANCE(arg)) {
            typeActual = className(AS_INSTANCE(arg)->klass);
        } else {
            typeActual = typeOfVal(arg);
        }
        throwArgErrorFmt("Expected argument %d to be of type %s, got: %s", argnum, typeExpect, typeActual);
    }
}
