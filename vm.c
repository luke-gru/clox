#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include "common.h"
#include "vm.h"
#include "debug.h"
#include "options.h"
#include "runtime.h"
#include "memory.h"
#include "compiler.h"
#include "nodes.h"

VM vm;

volatile pthread_t GVLOwner;

void vm_debug(int lvl, const char *format, ...) {
    if (GET_OPTION(debugVMLvl) < lvl) return;
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "[VM]: ");
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}
void thread_debug(int lvl, const char *format, ...) {
#ifndef NDEBUG
    if (!CLOX_OPTION_T(debugThreads)) return; // TODO: incorporate lvl
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "[TH]: ");
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    fflush(stderr);
#endif
}
void vm_warn(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "[Warning]: ");
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static void stacktraceHandler(int sig, siginfo_t *si, void *unused) {
    fprintf(stderr, "Got SIGSEGV at address: 0x%lx\n", (long)si->si_addr);
    fprintf(stderr, "VM initialized: %s\n", vm.inited ? "true" : "false");
    diePrintCBacktrace("%s", "info:");
}

void initCoreSighandlers(void) {
    struct sigaction sa;

    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = stacktraceHandler;
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        fprintf(stderr, "[WARNING]: SIGSEGV signal handler could not bet set.\n");
    }
}

char *unredefinableGlobals[] = {
    "clock",
    "typeof",
    "classof",
    "debugger",
    "loadScript",
    "requireScript",
    "eval",
    "yield",
    "blockGiven",
    "sleep",
    NULL,
};

static bool isUnredefinableGlobal(char *name) {
    ASSERT(name);
    char **glbl = unredefinableGlobals;
    while (*glbl != NULL) {
        if (strcmp(name, *glbl) == 0) {
            return true;
        }
        glbl++;
    }
    return false;
}

static void defineNativeFunctions(void) {
    addGlobalFunction("clock", lxClock);
    addGlobalFunction("typeof", lxTypeof);
    addGlobalFunction("classof", lxClassof);
    addGlobalFunction("loadScript", lxLoadScript);
    addGlobalFunction("requireScript", lxRequireScript);
    addGlobalFunction("autoload", lxAutoload);
    addGlobalFunction("debugger", lxDebugger);
    addGlobalFunction("eval", lxEval);
    addGlobalFunction("sleep", lxSleep);
    addGlobalFunction("yield", lxYield);
    addGlobalFunction("blockGiven", lxBlockGiven);
    addGlobalFunction("exit", lxExit);
    addGlobalFunction("_exit", lx_Exit);
    addGlobalFunction("atExit", lxAtExit);
    addGlobalFunction("alias", lxAlias);
    // TODO: make Thread() and Thread.join
    addGlobalFunction("newThread", lxNewThread);
    addGlobalFunction("joinThread", lxJoinThread);
    Init_rand();
}

// Builtin classes initialized below
ObjClass *lxObjClass;
ObjClass *lxClassClass;
ObjClass *lxModuleClass;
ObjClass *lxIteratorClass;
ObjModule *lxGCModule;
ObjClass *lxErrClass;
ObjClass *lxArgErrClass;
ObjClass *lxTypeErrClass;
ObjClass *lxNameErrClass;
ObjClass *lxSyntaxErrClass;
ObjClass *lxSystemErrClass;
ObjClass *lxLoadErrClass;

// internal error classes for flow control of blocks
ObjClass *lxBlockIterErrClass;
ObjClass *lxBreakBlockErrClass;
ObjClass *lxContinueBlockErrClass;
ObjClass *lxReturnBlockErrClass;
ObjClass *lxRecursionErrClass;

ObjArray *lxLoadPath; // load path for loadScript/requireScript (-L flag)
ObjArray *lxArgv;

ObjNative *nativeObjectInit;
ObjNative *nativeIteratorInit;
ObjNative *nativeErrorInit;
ObjNative *nativeClassInit;
ObjNative *nativeModuleInit;

bool isClassHierarchyCreated = false;

static void defineNativeClasses(void) {
    isClassHierarchyCreated = false;

    // class Object
    ObjClass *objClass = addGlobalClass("Object", NULL);
    lxObjClass = objClass;
    nativeObjectInit = addNativeMethod(objClass, "init", lxObjectInit);
    addNativeMethod(objClass, "dup", lxObjectDup);
    addNativeMethod(objClass, "extend", lxObjectExtend);
    addNativeMethod(objClass, "hashKey", lxObjectHashKey);
    addNativeMethod(objClass, "opEquals", lxObjectOpEquals);
    addNativeMethod(objClass, "isSame", lxObjectIsSame);
    addNativeMethod(objClass, "freeze", lxObjectFreeze);
    addNativeMethod(objClass, "unfreeze", lxObjectUnfreeze);
    addNativeMethod(objClass, "isFrozen", lxObjectIsFrozen);
    addNativeMethod(objClass, "send", lxObjectSend);
    addNativeMethod(objClass, "getProperty", lxObjectGetProperty);
    addNativeMethod(objClass, "setProperty", lxObjectSetProperty);
    addNativeMethod(objClass, "hasProperty", lxObjectHasProperty);
    addNativeMethod(objClass, "hasGetter", lxObjectHasGetter);
    addNativeMethod(objClass, "hasSetter", lxObjectHasSetter);
    addNativeMethod(objClass, "respondsTo", lxObjectRespondsTo);
    addNativeMethod(objClass, "inspect", lxObjectInspect);
    addNativeMethod(objClass, "instanceEval", lxObjectInstanceEval);
    addNativeGetter(objClass, "class", lxObjectGetClass);
    addNativeGetter(objClass, "singletonClass", lxObjectGetSingletonClass);
    addNativeGetter(objClass, "objectId", lxObjectGetObjectId);

    // class Module
    ObjClass *modClass = addGlobalClass("Module", objClass);
    lxModuleClass = modClass;

    // TODO: make `class` inherit from `module`
    // class Class
    ObjClass *classClass = addGlobalClass("Class", objClass);
    lxClassClass = classClass;

    // restore `klass` property of above-created classes, since <class Class>
    // is now created
    objClass->klass = classClass;
    modClass->klass = classClass;
    classClass->klass = classClass;

    Init_StringClass();

    // class Class
    nativeClassInit = addNativeMethod(classClass, "init", lxClassInit);
    addNativeMethod(classClass, "inspect", lxClassInspect);
    addNativeMethod(classClass, "methodAdded", lxClassMethodAdded);
    addNativeMethod(classClass, "constDefined", lxClassConstDefined);
    addNativeMethod(classClass, "constGet", lxClassConstGet);
    addNativeMethod(classClass, "defineMethod", lxClassDefineMethod);
    addNativeMethod(classClass, "constants", lxClassConstants);
    addNativeMethod(classClass, "ancestors", lxClassAncestors);
    addNativeMethod(classClass, "isA", lxClassIsA);
    addNativeMethod(classClass, "include", lxClassInclude);
    addNativeMethod(classClass, "aliasMethod", lxClassAliasMethod);
    addNativeGetter(classClass, "superClass", lxClassGetSuperclass);
    addNativeGetter(classClass, "name", lxClassGetName);

    nativeModuleInit = addNativeMethod(modClass, "init", lxModuleInit);
    addNativeMethod(modClass, "inspect", lxModuleInspect);
    addNativeGetter(modClass, "name", lxModuleGetName);

    Init_ArrayClass();
    Init_MapClass();
    // NOTE: all other inits need to be below, at the bottom of this function

    // class Iterator
    ObjClass *iterClass = addGlobalClass("Iterator", objClass);
    lxIteratorClass = iterClass;
    nativeIteratorInit = addNativeMethod(iterClass, "init", lxIteratorInit);
    addNativeMethod(iterClass, "next", lxIteratorNext);

    // class Error
    ObjClass *errClass = addGlobalClass("Error", objClass);
    lxErrClass = errClass;
    nativeErrorInit = addNativeMethod(errClass, "init", lxErrInit);

    // class ArgumentError
    ObjClass *argErrClass = addGlobalClass("ArgumentError", errClass);
    lxArgErrClass = argErrClass;

    // class TypeError
    ObjClass *typeErrClass = addGlobalClass("TypeError", errClass);
    lxTypeErrClass = typeErrClass;

    // class NameError
    ObjClass *nameErrClass = addGlobalClass("NameError", errClass);
    lxNameErrClass = nameErrClass;

    // class SyntaxError
    ObjClass *syntaxErrClass = addGlobalClass("SyntaxError", errClass);
    lxSyntaxErrClass = syntaxErrClass;

    // class SystemError
    ObjClass *systemErrClass = addGlobalClass("SystemError", errClass);
    lxSystemErrClass = systemErrClass;

    // class LoadError
    ObjClass *loadErrClass = addGlobalClass("LoadError", errClass);
    lxLoadErrClass = loadErrClass;

    // internal errors for block flow control
    ObjClass *blockIterErr = newClass(INTERN("BlockIterError"), errClass, NEWOBJ_FLAG_OLD|NEWOBJ_FLAG_HIDDEN);
    ObjClass *breakBlockErr = newClass(INTERN("BlockBreakError"), blockIterErr, NEWOBJ_FLAG_OLD|NEWOBJ_FLAG_HIDDEN);
    lxBreakBlockErrClass = breakBlockErr;
    ObjClass *continueBlockErr = newClass(INTERN("BlockContinueError"), blockIterErr, NEWOBJ_FLAG_OLD|NEWOBJ_FLAG_HIDDEN);
    ObjClass *returnBlockErr = newClass(INTERN("BlockReturnError"), blockIterErr, NEWOBJ_FLAG_OLD|NEWOBJ_FLAG_HIDDEN);
    hideFromGC(TO_OBJ(returnBlockErr));
    lxBlockIterErrClass = blockIterErr;
    lxContinueBlockErrClass = continueBlockErr;
    lxReturnBlockErrClass = returnBlockErr;

    lxRecursionErrClass = newClass(INTERN("RecursionError"), errClass, NEWOBJ_FLAG_OLD|NEWOBJ_FLAG_HIDDEN);

    // module GC
    ObjModule *GCModule = addGlobalModule("GC");
    ObjClass *GCClassStatic = moduleSingletonClass(GCModule);
    addNativeMethod(GCClassStatic, "stats", lxGCStats);
    addNativeMethod(GCClassStatic, "collect", lxGCCollect);
    addNativeMethod(GCClassStatic, "collectYoung", lxGCCollectYoung);
    addNativeMethod(GCClassStatic, "on", lxGCOn);
    addNativeMethod(GCClassStatic, "off", lxGCOff);
    addNativeMethod(GCClassStatic, "setFinalizer", lxGCSetFinalizer);
    lxGCModule = GCModule;

    // order of initialization not important here
    Init_RegexClass();
    Init_ProcessModule();
    Init_SignalModule();
    Init_IOClass();
    Init_FileClass();
    Init_DirClass();
    Init_ThreadClass();
    Init_TimeClass();
    Init_BlockClass();
    Init_SocketClass();
    Init_BindingClass();
    Init_ErrorClasses();
    isClassHierarchyCreated = true;
}

static void addToDefaultLoadPath(Value loadPathVal, const char *dir) {
    // add 'lib' path as default load path
    ObjString *path = hiddenString("", 0, NEWOBJ_FLAG_OLD);
    // FIXME: this won't work if user moves their clox binary after building
    pushCString(path, QUOTE(LX_BUILT_DIR), strlen(QUOTE(LX_BUILT_DIR)));
    pushCStringFmt(path, "/%s", dir);
    arrayPush(loadPathVal, OBJ_VAL(path));
    unhideFromGC(TO_OBJ(path));
}

// NOTE: this initialization function can create Lox runtime objects
static void defineGlobalVariables(void) {
    Value loadPathVal = newArray();
    ASSERT(IS_ARRAY(loadPathVal));
    lxLoadPath = AS_ARRAY(loadPathVal);
    ObjString *loadPathStr = INTERN("loadPath");
    ASSERT(tableSet(&vm.globals, OBJ_VAL(loadPathStr), loadPathVal));

    addToDefaultLoadPath(loadPathVal, "lib");
    addToDefaultLoadPath(loadPathVal, "ext");

    // populate load path from -L option given to commandline
    char *lpath = GET_OPTION(initialLoadPath);
    if (lpath && strlen(lpath) > 0) {
        char *beg = lpath;
        char *end = NULL;
        while ((end = strchr(beg, ':'))) {
            ObjString *str = copyString(beg, end - beg, NEWOBJ_FLAG_OLD);
            arrayPush(loadPathVal, OBJ_VAL(str));
            beg = end+1;
        }
    }

    ObjString *argvStr = INTERN("ARGV");
    Value argvVal = newArray();
    ASSERT(IS_ARRAY(argvVal));
    lxArgv = AS_ARRAY(argvVal);
    ASSERT(tableSet(&vm.globals, OBJ_VAL(argvStr), argvVal));
    ASSERT(origArgv);
    ASSERT(origArgc >= 1);
    for (int i = 0; i < origArgc; i++) {
        ObjString *argStr = copyString(origArgv[i], strlen(origArgv[i]), NEWOBJ_FLAG_OLD);
        Value arg = OBJ_VAL(argStr);
        arrayPush(OBJ_VAL(lxArgv), arg);
    }
    hideFromGC(TO_OBJ(lxArgv));
    hideFromGC(TO_OBJ(lxLoadPath));
}

static bool isIterableType(Value val) {
    if (IS_AN_ARRAY(val) || IS_A_MAP(val) || IS_INSTANCE(val)) {
        return true;
    };
    return false;
}

static bool isIterator(Value val) {
    return IS_A(val, lxIteratorClass);
}

static Value iteratorNext(Value iterator) {
    return lxIteratorNext(1, &iterator);
}

// NOTE: argument must be an iterable type (see isIterableType);
Value createIterator(Value iterable) {
    ASSERT(isIterableType(iterable));
    if (IS_AN_ARRAY(iterable) || IS_A_MAP(iterable)) {
        ObjInstance *iterObj = newInstance(lxIteratorClass, NEWOBJ_FLAG_NONE);
        callVMMethod(iterObj, OBJ_VAL(nativeIteratorInit), 1, &iterable, NULL);
        return pop();
    } else if (IS_INSTANCE(iterable)) {
        ObjString *iterId = INTERNED("iter", 4);
        ObjInstance *instance = AS_INSTANCE(iterable);
        Obj *iterMethod = instanceFindMethod(instance, iterId);
        Value ret;
        if (iterMethod) { // called iter(), it should return an iterator or an array/map
            callVMMethod(instance, OBJ_VAL(iterMethod), 0, NULL, NULL);
            ret = pop();
        } else {
            ObjString *iterNextId = INTERNED("iterNext", 8);
            Obj *iterNextMethod = instanceFindMethodOrRaise(instance, iterNextId);
            (void)iterNextMethod; // just check for existence of iterNext method
            ObjInstance *iterObj = newInstance(lxIteratorClass, NEWOBJ_FLAG_NONE);
            callVMMethod(iterObj, OBJ_VAL(nativeIteratorInit), 1, &iterable, NULL);
            return pop();
        }
        if (IS_AN_ARRAY(ret) || IS_A_MAP(ret)) {
            return createIterator(ret);
        } else if (isIterator(ret)) {
            return ret;
        } else {
            throwErrorFmt(lxTypeErrClass, "Return value from iter() must be an Iterator or iterable value (Array/Map)");
        }
    }
    UNREACHABLE(__func__);
}

// Uncaught errors jump here
static jmp_buf rootVMLoopJumpBuf;
static bool rootVMLoopJumpBufSet = false;

static int curLine = 1; // TODO: per thread

// Add and use a new execution context. Execution contexts
// hold the value stack.
static inline void push_EC(bool allocateStack) {
    LxThread *th = vm.curThread;
    VMExecContext *ectx = ALLOCATE(VMExecContext, 1);
    memset(ectx, 0, sizeof(*ectx));
    initTable(&ectx->roGlobals);
    ectx->frameCount = 0;
    if (allocateStack) {
      ectx->stack = ALLOCATE(Value, STACK_MAX);
      ectx->stackAllocated = true;
      ectx->stackTop = ectx->stack;
    }
    vec_push(&th->v_ecs, ectx);
    th->ec = ectx; // EC = ectx
}

// Pop the current execution context
static inline void pop_EC(void) {
    LxThread *th = vm.curThread;
    ASSERT(th->v_ecs.length > 0);
    VMExecContext *ctx = (VMExecContext*)vec_pop(&th->v_ecs);
    if (ctx->stackAllocated) {
      FREE_SIZE(sizeof(Value)*STACK_MAX, ctx->stack);
    }
    freeTable(&ctx->roGlobals);
    FREE(VMExecContext, ctx);
    if (th->v_ecs.length == 0) {
        th->ec = NULL;
    } else {
        th->ec = (VMExecContext*)vec_last(&th->v_ecs);
    }
}

static inline bool isInEval(void) {
    return EC->evalContext;
}

static inline bool isInLoadedScript(void) {
    return EC->loadContext;
}

// reset (clear) value stack for current execution context
void resetStack(void) {
    ASSERT(EC);
    EC->stackTop = EC->stack;
    EC->frameCount = 0;
}

static ObjInstance *initMainThread(void) {
    if (pthread_mutex_init(&vm.GVLock, NULL) != 0) {
        die("Global VM lock unable to initialize");
    }
    if (pthread_cond_init(&vm.GVLCond, NULL) != 0) {
        die("Global VM lock cond unable to initialize");
    }
    vm.GVLockStatus = 0;
    vm.GVLWaiters = 0;
    vm.curThread = NULL;
    vm.mainThread = NULL;
    vm.numDetachedThreads = 0;
    vm.numLivingThreads = 0;

    Value mainThread = newThread();
    LxThread *th = THREAD_GETHIDDEN(mainThread);

    vm.curThread = th;
    vm.mainThread = th;
    vec_init(&vm.threads);
    vec_push(&vm.threads, AS_INSTANCE(mainThread));

    pthread_t tid = pthread_self();
    threadSetId(mainThread, tid);
    vm.numLivingThreads++;
    threadSetStatus(mainThread, THREAD_RUNNING);
    acquireGVL();
    THREAD_DEBUG(1, "Main thread initialized (%lu)", tid);
    return AS_INSTANCE(mainThread);
}

void initVM() {
    if (vm.inited) {
        VM_WARN("initVM: VM already initialized");
        return;
    }
    VM_DEBUG(1, "initVM() start");
    turnGCOff();
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    initTable(&vm.globals);
    initTable(&vm.strings); // interned strings
    initTable(&vm.regexLiterals);
    initTable(&vm.constants);
    initTable(&vm.autoloadTbl);
    vec_init(&vm.hiddenObjs);

    ObjInstance *mainT = initMainThread();
    push_EC(true);
    resetStack();

    vec_init(&vm.loadedScripts);
    vm.printBuf = NULL;
    curLine = 1;

    memset(&rootVMLoopJumpBuf, 0, sizeof(rootVMLoopJumpBuf));
    rootVMLoopJumpBufSet = false;

    vec_init(&vm.exitHandlers);

    initDebugger(&vm.debugger);
    vm.instructionStepperOn = CLOX_OPTION_T(stepVMExecution);

    vm.inited = true; // NOTE: VM has to be inited before creation of strings
    vm.exited = false;
    vm.exiting = false;
    vm.initString = INTERNED("init", 4);
    vm.fileString = INTERNED("__FILE__", 8);
    vm.dirString  = INTERNED("__DIR__", 7);
    vm.funcString = INTERNED("__FUNC__",  8);
    vm.mainString = INTERNED("(main)",  6);
    vm.anonString = INTERNED("(anon)",  6);
    vm.opAddString = INTERNED("opAdd", 5);
    vm.opDiffString = INTERNED("opDiff", 6);
    vm.opMulString = INTERNED("opMul", 5);
    vm.opDivString = INTERNED("opDiv", 5);
    vm.opShovelLString = INTERNED("opShovelLeft", 12);
    vm.opShovelRString = INTERNED("opShovelRight", 13);
    vm.opIndexGetString = INTERNED("opIndexGet", 10);
    vm.opIndexSetString = INTERNED("opIndexSet", 10);
    vm.opEqualsString = INTERNED("opEquals", 8);
    vm.opCmpString = INTERNED("opCmp", 5);

    pushFrame(NULL);

    defineNativeFunctions();
    defineNativeClasses();
    mainT->klass = lxThreadClass; // now that it's created
    defineGlobalVariables();

    popFrame();

    // Some of these strings were created Before lxStringClass was assigned to.
    Entry e;
    int strIdx = 0;
    TABLE_FOREACH(&vm.strings, e, strIdx, {
        AS_STRING(e.key)->klass = lxStringClass;
    })

    resetStack();
    // don't pop EC here, we'll pop it in freeVM()

    turnGCOn();
    VM_DEBUG(1, "initVM() end");
}

void freeVM(void) {
    /*fprintf(stderr, "VM run level: %d\n", th->vmRunLvl);*/
    if (!vm.inited) {
        VM_WARN("freeVM: VM not yet initialized");
        return;
    }
    VM_DEBUG(1, "freeVM() start");

    removeVMSignalHandlers();
    freeObjects();

    freeDebugger(&vm.debugger);
    vm.instructionStepperOn = false;

    curLine = 1;

    memset(&rootVMLoopJumpBuf, 0, sizeof(rootVMLoopJumpBuf));
    rootVMLoopJumpBufSet = false;

    freeTable(&vm.globals);
    freeTable(&vm.regexLiterals);
    freeTable(&vm.strings);
    freeTable(&vm.constants);
    freeTable(&vm.autoloadTbl);
    vm.initString = NULL;
    vm.fileString = NULL;
    vm.dirString = NULL;
    vm.funcString = NULL;
    vm.mainString = NULL;
    vm.anonString = NULL;
    vm.opAddString = NULL;
    vm.opDiffString = NULL;
    vm.opMulString = NULL;
    vm.opDivString = NULL;
    vm.opShovelLString = NULL;
    vm.opShovelRString = NULL;
    vm.opIndexGetString = NULL;
    vm.opIndexSetString = NULL;
    vm.opEqualsString = NULL;
    vm.opCmpString = NULL;
    vm.printBuf = NULL;
    vm.printToStdout = true;
    vec_deinit(&vm.hiddenObjs);
    vec_deinit(&vm.loadedScripts);
    isClassHierarchyCreated = false;

    vm.inited = false;
    vm.exited = false;

    vec_deinit(&vm.exitHandlers);

    while (THREAD()->ec) { pop_EC(); }

    THREAD_DEBUG(1, "VM lock destroying... # threads: %d, # waiters: %d", vm.threads.length, vm.GVLWaiters);
    releaseGVL(THREAD_ZOMBIE);
    if (vm.numDetachedThreads <= 0) {
        pthread_mutex_destroy(&vm.GVLock);
        pthread_cond_destroy(&vm.GVLCond);
    }
    vm.GVLWaiters = 0;

    vm.curThread = NULL;
    vm.mainThread = NULL;
    vec_deinit(&vm.threads);

    nativeObjectInit = NULL;
    nativeStringInit = NULL;
    nativeArrayInit = NULL;
    nativeMapInit = NULL;
    nativeRegexInit = NULL;

    VM_DEBUG(1, "freeVM() end");
}

int VMNumStackFrames(void) {
    VMExecContext *ctx; int ctxIdx = 0;
    int num = 0;
    vec_foreach(&THREAD()->v_ecs, ctx, ctxIdx) {
        ASSERT(ctx->stackTop >= ctx->stack);
        num += (ctx->stackTop - ctx->stack);
    }
    return num;
}

int VMNumCallFrames(void) {
    int ret = 0;
    VMExecContext *ec; int i = 0;
    vec_foreach(&THREAD()->v_ecs, ec, i) {
        ret += ec->frameCount;
    }
    return ret;
}

bool VMLoadedScript(char *fname) {
    DBG_ASSERT(vm.inited);
    Value *loaded = NULL; int i = 0;
    vec_foreach_ptr(&vm.loadedScripts, loaded, i) {
        if (strcmp(fname, VAL_TO_STRING(*loaded)->chars) == 0) {
            return true;
        }
    }
    return false;
}

#define ASSERT_VALID_STACK() DBG_ASSERT(EC->stackTop >= EC->stack && EC->stackTop < EC->stack + STACK_MAX)

static inline bool isOpStackEmpty(void) {
    ASSERT_VALID_STACK();
    return EC->stackTop == EC->stack;
}

#define VM_PUSH(val) vm_push(th, ctx, val)
#define VM_POP() vm_pop(th, ctx)
#define VM_POPN(n) vm_popN(th, ctx, n)
#define VM_PEEK(n) vm_peek(th, ctx, n)
#define VM_PUSHSWAP(val) vm_pushSwap(th, ctx, val)

void push(Value value) {
    ASSERT_VALID_STACK();
    register VMExecContext *ctx = EC;
    if (UNLIKELY(ctx->stackTop >= ctx->stack + STACK_MAX)) {
        errorPrintScriptBacktrace("Stack overflow.");
        int status = 1;
        vm.curThread->status = THREAD_ZOMBIE;
        vm.numLivingThreads--;
        pthread_exit(&status);
    }
    if (IS_OBJ(value)) {
        DBG_ASSERT(AS_OBJ(value)->type != OBJ_T_NONE);
        OBJ_SET_PUSHED_VM_STACK(AS_OBJ(value)); // for gen gc
    }
    *ctx->stackTop = value;
    ctx->stackTop++;
}

static inline void vm_push(register LxThread *th, register VMExecContext *ctx, Value value) {
    ASSERT_VALID_STACK();
    if (UNLIKELY(ctx->stackTop >= ctx->stack + STACK_MAX)) {
        errorPrintScriptBacktrace("Stack overflow.");
        int status = 1;
        th->status = THREAD_ZOMBIE;
        vm.numLivingThreads--;
        pthread_exit(&status);
    }
    if (IS_OBJ(value)) {
        DBG_ASSERT(AS_OBJ(value)->type != OBJ_T_NONE);
        OBJ_SET_PUSHED_VM_STACK(AS_OBJ(value)); // for gen gc
    }
    *ctx->stackTop = value;
    ctx->stackTop++;
}

// NOTE: doesn't perform checks to see if there's at least 1 item on the op stack
static inline void vm_pushSwap(register LxThread *th, register VMExecContext *ctx, Value value) {
    if (IS_OBJ(value)) {
        DBG_ASSERT(AS_OBJ(value)->type != OBJ_T_NONE);
        OBJ_SET_PUSHED_VM_STACK(AS_OBJ(value)); // for gen gc
    }
    *(ctx->stackTop-1) = value;
}

Value pop(void) {
    VMExecContext *ctx = EC;
    ASSERT(ctx->stackTop > ctx->stack);
    ctx->stackTop--;
    ctx->lastValue = ctx->stackTop;
    vm.curThread->lastValue = ctx->lastValue;
    return *(vm.curThread->lastValue);
}

static inline Value vm_pop(register LxThread *th, register VMExecContext *ctx) {
    ASSERT(ctx->stackTop > ctx->stack);
    ctx->stackTop--;
    ctx->lastValue = ctx->stackTop;
    th->lastValue = ctx->lastValue;
    return *(th->lastValue);
}

static inline Value vm_popN(register LxThread *th, register VMExecContext *ctx, int n) {
    ASSERT((ctx->stackTop-n) >= ctx->stack);
    ctx->stackTop-=n;
    ctx->lastValue = ctx->stackTop;
    th->lastValue = ctx->lastValue;
    return *(th->lastValue);
}

Value peek(unsigned n) {
    VMExecContext *ctx = EC;
    ASSERT((ctx->stackTop-n) > ctx->stack);
    return *(ctx->stackTop-1-n);
}

static inline Value vm_peek(register LxThread *th, register VMExecContext *ctx, unsigned n) {
    ASSERT((ctx->stackTop-n) > ctx->stack);
    return *(ctx->stackTop-1-n);
}

static inline void setThis(unsigned n) {
    register VMExecContext *ctx = EC;
    register LxThread *th = vm.curThread;
    if (UNLIKELY((ctx->stackTop-n) <= ctx->stack)) {
        ASSERT(0);
    }
    th->thisObj = AS_OBJ(*(ctx->stackTop-1-n));
    getFrame()->instance = (ObjInstance*)th->thisObj;
    vec_push(&th->v_thisStack, th->thisObj);
    DBG_ASSERT(th->thisObj);
}

static inline void popThis() {
    register LxThread *th = vm.curThread;
    ASSERT(th->v_thisStack.length > 0);
    (void)vec_pop(&th->v_thisStack);
    if (th->v_thisStack.length > 0) {
        th->thisObj = vec_last(&th->v_thisStack);
     } else {
        th->thisObj = NULL;
     }
    getFrame()->instance = TO_INSTANCE(th->thisObj);
}

static inline void pushCref(ObjClass *klass) {
    vec_push(&vm.curThread->v_crefStack, klass);
}

Value *getLastValue(void) {
    if (isOpStackEmpty()) {
        return EC->lastValue;
    } else {
        return EC->stackTop-1;
    }
}

static inline Value nilValue() {
    return NIL_VAL;
}

static inline Value trueValue() {
#ifdef NAN_TAGGING
    return TRUE_VAL;
#else
    return BOOL_VAL(true);
#endif
}

static inline Value falseValue() {
#ifdef NAN_TAGGING
    return FALSE_VAL;
#else
    return BOOL_VAL(false);
#endif
}

static inline bool canCmpValues(Value lhs, Value rhs, uint8_t cmpOp) {
    if ( (IS_NUMBER(lhs) && IS_NUMBER(rhs)) ||
        (IS_STRING(lhs) && IS_STRING(rhs)) ) {
        return true;
    // should respond to opCmp, but will throw if doesn't and it's called on the
    // object so okay to return true here
    } else if (IS_INSTANCE_LIKE(lhs)) {
        return true;
    } else {
        return false;
    }
}

// returns -1, 0, 1, or -2 on error
static int cmpValues(Value lhs, Value rhs, uint8_t cmpOp) {
    if (IS_NUMBER(lhs) && IS_NUMBER(rhs)) {
        double numA = AS_NUMBER(lhs);
        double numB = AS_NUMBER(rhs);
        if (numA == numB) {
            return 0;
        } else if (numA < numB) {
            return -1;
        } else {
            return 1;
        }
    } else if (IS_STRING(lhs) && IS_STRING(rhs)) {
        ObjString *lhsStr = AS_STRING(lhs);
        ObjString *rhsStr = AS_STRING(rhs);
        return strcmp(lhsStr->chars, rhsStr->chars);
    } else {
        Value ret = callMethod(AS_OBJ(lhs), vm.opCmpString, 1, &rhs, NULL);
        if (!IS_NUMBER(ret)) {
            throwErrorFmt(lxTypeErrClass, "Expected number returned from opCmp");
        }
        return (int)AS_NUMBER(ret);
    }

    UNREACHABLE_RETURN(-2);
}

static bool isValueOpEqual(Value lhs, Value rhs) {
#ifdef NAN_TAGGING
#else
    if (lhs.type != rhs.type) {
        return false;
    }
#endif
    if (IS_OBJ(lhs)) {
        if (IS_INSTANCE_LIKE(lhs)) {
            ObjString *opEquals = vm.opEqualsString;
            ObjInstance *self = AS_INSTANCE(lhs);
            Obj *methodOpEq = instanceFindMethod(self, opEquals);
            Obj *methodOpCmp;
            if (methodOpEq != NULL) {
                Value ret = callVMMethod(self, OBJ_VAL(methodOpEq), 1, &rhs, NULL);
                pop();
                return isTruthy(ret);
            } else if ((methodOpCmp = instanceFindMethod(self, vm.opCmpString))) {
                Value ret = callVMMethod(self, OBJ_VAL(methodOpCmp), 1, &rhs, NULL);
                pop();
                return IS_NUMBER(ret) && (int)AS_NUMBER(ret) == 0;
            }
        }
        // 2 objects, same pointers to Obj are equal
        return AS_OBJ(lhs) == AS_OBJ(rhs);
    } else if (IS_NUMBER(lhs)) { // 2 numbers, same values are equal
#ifdef NAN_TAGGING
        return lhs == rhs;
#else
        return AS_NUMBER(lhs) == AS_NUMBER(rhs);
#endif
    } else if (IS_NIL(lhs)) { // 2 nils, are equal
        return IS_NIL(rhs);
    } else if (IS_BOOL(lhs)) {
#ifdef NAN_TAGGING
        return lhs == rhs;
#else
        return AS_BOOL(lhs) == AS_BOOL(rhs);
#endif
    } else {
#ifdef NAN_TAGGING
        return lhs == rhs;
#else
        return false; // type check was made way above, so false here
#endif
    }
}

void debugFrame(CallFrame *frame) {
    const char *fnName = frame->isCCall ? frame->nativeFunc->name->chars :
        (frame->closure->function->name ? frame->closure->function->name->chars : "(anon)");
    fprintf(stderr, "CallFrame:\n");
    fprintf(stderr, "  name: %s\n", fnName);
    fprintf(stderr, "  native? %c\n", frame->isCCall ? 't' : 'f');
    fprintf(stderr, "  method? %c\n", frame->instance ? 't' : 'f');
    if (frame->klass) {
        fprintf(stderr, "  class: %s\n", CLASSINFO(frame->klass)->name ? CLASSINFO(frame->klass)->name->chars : "(anon)");
    }
}

static inline CallFrame *getFrameOrNull(void) {
    if (EC->frameCount == 0) {
        return NULL;
    }
    return &EC->frames[EC->frameCount-1];
}

static inline Chunk *currentChunk(void) {
    return getFrame()->closure->function->chunk;
}

void errorPrintScriptBacktrace(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    // TODO: go over all execution contexts
    for (int i = EC->frameCount - 1; i >= 0; i--) {
        CallFrame *frame = &EC->frames[i];
        if (frame->isCCall) {
            ObjNative *nativeFunc = frame->nativeFunc;
            ASSERT(nativeFunc);
            fprintf(stderr, "in native function %s()\n", nativeFunc->name->chars);
        } else {
            ObjFunction *function = frame->closure->function;
            // -1 because the IP is sitting on the next instruction to be executed.
            size_t instruction = frame->ip - function->chunk->code - 1;
            fprintf(stderr, "[line %d] in ", function->chunk->lines[instruction]);
            if (function->name == NULL) {
                fprintf(stderr, "script\n"); // top-level
            } else {
                char *fnName = function->name ? function->name->chars : "(anon)";
                fprintf(stderr, "%s()\n", fnName);
            }
        }
    }

    THREAD()->hadError = true;
    resetStack();
}

static void showUncaughtError(Value err) {
    ObjString *classNameObj = classNameFull(AS_INSTANCE(err)->klass);
    char *className = NULL;
    if (classNameObj) {
        className = classNameObj->chars;
    } else {
        className = "(anon)";
    }
    Value msg = getProp(err, INTERNED("message", 7));
    char *msgStr = NULL;
    if (!IS_NIL(msg)) {
        msgStr = VAL_TO_STRING(msg)->chars;
    }
    Value bt = getProp(err, INTERNED("backtrace", 9));
    ASSERT(!IS_NIL(bt));
    int btSz = ARRAY_SIZE(bt);
    fprintf(stderr, "Uncaught error, class: %s\n", className);
    if (msgStr) {
        fprintf(stderr, "Message: \"%s\"\n", msgStr);
    } else {
        fprintf(stderr, "Message: none\n");
    }
    fprintf(stderr, "Backtrace:\n");
    for (int i = 0; i < btSz; i++) {
        fprintf(stderr, "%s", VAL_TO_STRING(ARRAY_GET(bt, i))->chars);
    }

    THREAD()->hadError = true;
    resetStack();
}

static inline bool isBlockControlFlow(Value err) {
    return IS_A(err, lxBlockIterErrClass);
}

void setBacktrace(Value err) {
    if (isBlockControlFlow(err)) {
        return;
    }
    VM_DEBUG(2, "Setting backtrace");
    LxThread *th = vm.curThread;
    DBG_ASSERT(IS_AN_ERROR(err));
    Value ret = newArray();
    setProp(err, INTERNED("backtrace", 9), ret);
    int numECs = th->v_ecs.length;
    VMExecContext *ctx;
    for (int i = numECs-1; i >= 0; i--) {
        ctx = th->v_ecs.data[i];
        DBG_ASSERT(ctx);
        for (int j = ctx->frameCount - 1; j >= 0; j--) {
            CallFrame *frame = &ctx->frames[j];
            int line = frame->callLine;
            ObjString *file = frame->file;
            ASSERT(file);
            ObjString *outBuf = hiddenString("", 0, NEWOBJ_FLAG_NONE);
            Value out = OBJ_VAL(outBuf);
            if (frame->isCCall) {
                ObjNative *nativeFunc = frame->nativeFunc;
                pushCStringFmt(outBuf, "%s:%d in ", file->chars, line);
                if (nativeFunc) {
                    pushCStringFmt(outBuf, "<%s (native)>\n",
                            nativeFunc->name->chars);
                } else {
                    pushCStringFmt(outBuf, "<%s (native)>\n", "?unknown?");
                }
            } else {
                ObjFunction *function = NULL;
                function = frame->closure ? frame->closure->function : NULL;
                pushCStringFmt(outBuf, "%s:%d in ", file->chars, line);
                // NOTE: function can be null in test cases
                if (!function || function->name == NULL) {
                    pushCString(outBuf, "<script>\n", 9); // top-level
                } else {
                    char *fnName = function->name ? function->name->chars : (char*)"(anon)";
                    pushCStringFmt(outBuf, "<%s>\n", fnName);
                }
            }
            arrayPush(ret, out);
            unhideFromGC(TO_OBJ(outBuf));
        }
    }
    VM_DEBUG(2, "/Setting backtrace");
}

static inline bool isThrowable(Value val) {
    return IS_AN_ERROR(val);
}

bool lookupMethod(ObjInstance *obj, Obj *klass, ObjString *propName, Value *ret, bool lookInGivenClass) {
    Obj *givenClass = klass;
    if (klass == TO_OBJ(obj->klass) && obj->singletonKlass) {
        klass = TO_OBJ(obj->singletonKlass);
    }
    Value key = OBJ_VAL(propName);
    Obj *classLookup = klass;
    while (classLookup) {
        if (!lookInGivenClass && classLookup == givenClass) {
            classLookup = CLASS_SUPER(classLookup);
            continue;
        }
        Table *mtable = CLASS_METHOD_TBL(classLookup);
        if (tableGet(mtable, key, ret)) {
            return true;
        }
        classLookup = CLASS_SUPER(classLookup);
    }
    return false;
}

static InterpretResult vm_run0(void);
static InterpretResult vm_run(void);

Value propertyGet(ObjInstance *obj, ObjString *propName) {
    Value ret;
    Obj *method = NULL;
    Obj *getter = NULL;
    if (tableGet(obj->fields, OBJ_VAL(propName), &ret)) {
        VM_DEBUG(3, "field found (propertyGet)");
        return ret;
    } else if ((getter = instanceFindGetter(obj, propName))) {
        VM_DEBUG(3, "getter found (propertyGet)");
        callVMMethod(obj, OBJ_VAL(getter), 0, NULL, NULL);
        if (vm.curThread->hadError) {
            return NIL_VAL;
        } else {
            return pop();
        }
    } else if ((method = instanceFindMethod(obj, propName))) {
        VM_DEBUG(3, "method found [bound] (propertyGet)");
        ObjBoundMethod *bmethod = newBoundMethod(obj, method, NEWOBJ_FLAG_NONE);
        return OBJ_VAL(bmethod);
    } else {
        return NIL_VAL;
    }
}

void propertySet(ObjInstance *obj, ObjString *propName, Value rval) {
    if (isFrozen(TO_OBJ(obj))) {
        throwErrorFmt(lxErrClass, "Tried to set property on frozen object");
    }
    Obj *setter = NULL;
    if ((setter = instanceFindSetter(obj, propName))) {
        VM_DEBUG(3, "setter found");
        callVMMethod(obj, OBJ_VAL(setter), 1, &rval, NULL);
        pop();
    } else {
        tableSet(obj->fields, OBJ_VAL(propName), rval);
        if (IS_OBJ(rval)) {
            OBJ_WRITE(OBJ_VAL(obj), rval);
        }
    }
}

static ObjFunction *funcFromCallable(Value callable) {
  if (IS_FUNCTION(callable)) { return AS_FUNCTION(callable); }
  if (IS_CLASS(callable)) {
    Obj *methodObj = instanceFindMethod(AS_INSTANCE(callable), INTERN("init"));
    if (!methodObj) { return NULL; }
    Value method = OBJ_VAL(methodObj);
    return funcFromCallable(method);
  } else if (IS_CLOSURE(callable)) {
    return AS_CLOSURE(callable)->function;
  } else if (IS_NATIVE_FUNCTION(callable)) {
    return NULL;
  } else if (IS_BOUND_METHOD(callable)) {
    ObjBoundMethod *bmethod = AS_BOUND_METHOD(callable);
    Obj *callable = bmethod->callable; // native function or user-defined function (ObjClosure)
    return funcFromCallable(OBJ_VAL(callable));
  } else {
    UNREACHABLE_RETURN(NULL);
  }
}

void defineMethod(Value classOrMod, ObjString *name, Value method) {
    ASSERT(isCallable(method));
    ObjFunction *func = funcFromCallable(method);
    // FIXME: a function can be attached to multiple classes because of
    // runtime defineMethod(). I don't think this field should exist anymore,
    // not sure if it's currently used anywhere.
    if (func) {
      func->klass = AS_OBJ(classOrMod);
    }
    if (IS_CLASS(classOrMod)) {
        ObjClass *klass = AS_CLASS(classOrMod);
        const char *klassName = CLASSINFO(klass)->name ? CLASSINFO(klass)->name->chars : "(anon)";
        (void)klassName;
        VM_DEBUG(2, "defining method '%s' in class '%s'", name->chars, klassName);
        tableSet(CLASSINFO(klass)->methods, OBJ_VAL(name), method);
        OBJ_WRITE(OBJ_VAL(klass), method);
        GC_OLD(AS_OBJ(method));
        Value methodName = OBJ_VAL(name);
        callMethod(AS_OBJ(classOrMod), INTERN("methodAdded"), 1, &methodName, NULL);
    } else if (IS_MODULE(classOrMod)) {
        ObjModule *mod = AS_MODULE(classOrMod);
        const char *modName = CLASSINFO(mod)->name ? CLASSINFO(mod)->name->chars : "(anon)";
        (void)modName;
        VM_DEBUG(2, "defining method '%s' in module '%s'", name->chars, modName);
        tableSet(CLASSINFO(mod)->methods, OBJ_VAL(name), method);
        OBJ_WRITE(OBJ_VAL(mod), method);
        GC_OLD(AS_OBJ(method));
    } else {
        UNREACHABLE("class type: %s", typeOfVal(classOrMod));
    }
}

static void defineStaticMethod(ObjString *name) {
    Value method = peek(0); // function
    ASSERT(IS_CLOSURE(method));
    ObjFunction *func = AS_CLOSURE(method)->function;
    Value classOrMod = peek(1);
    ASSERT(IS_CLASS(classOrMod) || IS_MODULE(classOrMod));
    func->klass = AS_OBJ(classOrMod);
    func->isSingletonMethod = true;
    ObjClass *metaClass = singletonClass(AS_OBJ(classOrMod));
    VM_DEBUG(2, "defining static method '%s#%s'", CLASSINFO(metaClass)->name->chars, name->chars);
    tableSet(CLASSINFO(metaClass)->methods, OBJ_VAL(name), method);
    OBJ_WRITE(OBJ_VAL(metaClass), method);
    GC_OLD(AS_OBJ(method));
    pop(); // function
}

static void defineGetter(ObjString *name) {
    Value method = peek(0); // function
    ASSERT(IS_CLOSURE(method));
    Value classOrMod = peek(1);
    ASSERT(IS_CLASS(classOrMod) || IS_MODULE(classOrMod));
    if (IS_CLASS(classOrMod)) {
        ObjClass *klass = AS_CLASS(classOrMod);
        VM_DEBUG(2, "defining getter '%s'", name->chars);
        tableSet(CLASSINFO(klass)->getters, OBJ_VAL(name), method);
        OBJ_WRITE(OBJ_VAL(klass), method);
        GC_OLD(AS_OBJ(method));
    } else {
        ObjModule *mod = AS_MODULE(classOrMod);
        VM_DEBUG(2, "defining getter '%s'", name->chars);
        tableSet(CLASSINFO(mod)->getters, OBJ_VAL(name), method);
        OBJ_WRITE(OBJ_VAL(mod), method);
        GC_OLD(AS_OBJ(method));
    }
    pop(); // function
}

static void defineSetter(ObjString *name) {
    Value method = peek(0); // function
    ASSERT(IS_CLOSURE(method));
    Value classOrMod = peek(1);
    if (IS_CLASS(classOrMod)) {
        ObjClass *klass = AS_CLASS(classOrMod);
        VM_DEBUG(2, "defining setter '%s'", name->chars);
        tableSet(CLASSINFO(klass)->setters, OBJ_VAL(name), method);
        OBJ_WRITE(OBJ_VAL(klass), method);
        GC_OLD(AS_OBJ(method));
    } else {
        ObjModule *mod = AS_MODULE(classOrMod);
        VM_DEBUG(2, "defining setter '%s'", name->chars);
        tableSet(CLASSINFO(mod)->setters, OBJ_VAL(name), method);
        OBJ_WRITE(OBJ_VAL(mod), method);
        GC_OLD(AS_OBJ(method));
    }
    pop(); // function
}

// Call method on instance, args are NOT expected to be pushed on to stack by
// caller, nor is the instance. `argCount` does not include the implicit instance argument.
// Return value is pushed to stack and returned.
Value callVMMethod(ObjInstance *instance, Value callable, int argCount, Value *args, CallInfo *cinfo) {
    VM_DEBUG(2, "Calling VM method");
    push(OBJ_VAL(instance));
    for (int i = 0; i < argCount; i++) {
        DBG_ASSERT(args); // can be null when if i = 0
        push(args[i]);
    }
    VM_DEBUG(3, "call begin");
    callCallable(callable, argCount, true, cinfo); // pushes return value to stack
    VM_DEBUG(3, "call end");
    return peek(0);
}

Value callMethod(Obj *obj, ObjString *methodName, int argCount, Value *args, CallInfo *cinfo) {
    if (obj->type == OBJ_T_INSTANCE || obj->type == OBJ_T_ARRAY || obj->type == OBJ_T_STRING || obj->type == OBJ_T_MAP || obj->type == OBJ_T_REGEX) {
        ObjInstance *instance = (ObjInstance*)obj;
        Obj *callable = instanceFindMethod(instance, methodName);
        if (!callable && argCount == 0) {
            callable = instanceFindGetter(instance, methodName);
        }
        if (UNLIKELY(!callable)) {
            ObjString *className = CLASSINFO(instance->klass)->name;
            const char *classStr = className->chars ? className->chars : "(anon)";
            // TODO: show full class name
            throwErrorFmt(lxErrClass, "instance method '%s#%s' not found", classStr, methodName->chars);
        }
        callVMMethod(instance, OBJ_VAL(callable), argCount, args, cinfo);
        return pop();
    } else if (obj->type == OBJ_T_CLASS) {
        ObjClass *klass = (ObjClass*)obj;
        Obj *callable = classFindStaticMethod(klass, methodName);
        if (!callable && argCount == 0) {
          callable = instanceFindGetter((ObjInstance*)klass, methodName);
        }
        if (!callable) {
            ObjString *className = CLASSINFO(klass)->name;
            const char *classStr = className ? className->chars : "(anon)";
            // TODO: show full class name
            throwErrorFmt(lxErrClass, "class method '%s.%s' not found", classStr, methodName->chars);
        }
        callVMMethod((ObjInstance*)klass, OBJ_VAL(callable), argCount, args, cinfo);
        return pop();
    } else if (obj->type == OBJ_T_MODULE) {
        ObjModule *mod = (ObjModule*)obj;
        Obj *callable = classFindStaticMethod((ObjClass*)mod, methodName);
        if (UNLIKELY(!callable)) {
            ObjString *modName = CLASSINFO(mod)->name;
            const char *modStr = modName ? modName->chars : "(anon)";
            // TODO: show full mod name
            throwErrorFmt(lxErrClass, "module method '%s.%s' not found", modStr, methodName->chars);
        }
        callVMMethod((ObjInstance*)mod, OBJ_VAL(callable), argCount, args, cinfo);
        return pop();
    } else {
        throwErrorFmt(lxTypeErrClass, "Tried to invoke method '%s' on non-instance (type=%s)", methodName->chars, typeOfObj(obj));
    }
}

Value callFunctionValue(Value callable, int argCount, Value *args) {
    ASSERT(isCallable(callable)); // TODO: throw error?
    push(callable);
    for (int i = 0; i < argCount; i++) {
        push(args[i]);
    }
    callCallable(callable, argCount, false, NULL);
    return pop();
}

static void unwindErrInfo(CallFrame *frame) {
    ErrTagInfo *info = vm.curThread->errInfo;
    while (info && info->frame == frame) {
        if (info->bentry) {
            /*fprintf(stderr, "Popping block entry (unwindErrInfo)\n");*/
            popBlockEntry(info->bentry);
        }
        ErrTagInfo *prev = info->prev;
        FREE(ErrTagInfo, info);
        info = prev;
    }
    vm.curThread->errInfo = info;
}

static void closeUpvalues(Value *last);

void popFrame(void) {
    DBG_ASSERT(vm.inited);
    register LxThread *th = vm.curThread;
    ASSERT(EC->frameCount >= 1);
    CallFrame *frame = getFrame();
    Value *newTop = frame->slots;
    VM_DEBUG(2, "popping callframe (%s)", frame->isCCall ? "native" : "non-native");
    int stackAdjust = frame->stackAdjustOnPop;
    /*VM_DEBUG("stack adjust: %d", stackAdjust);*/
    unwindErrInfo(frame);
    if (frame->isCCall) {
        DBG_ASSERT(th->inCCall > 0);
        th->inCCall--;
        if (th->inCCall == 0) {
            vec_clear(&vm.curThread->stackObjects);
        }
    } else {
        closeUpvalues(newTop);
    }
    frame->popped = true;
    if (frame->klass != NULL) {
        ASSERT(th->v_crefStack.length > 0);
        VM_DEBUG(3, "popping cref from popFrame");
        (void)vec_pop(&th->v_crefStack);
    }
    if (frame->instance != NULL) {
        VM_DEBUG(3, "popping this from popFrame");
        popThis();
    }
    frame->scope = NULL;
    EC->frameCount--;
    frame = getFrameOrNull(); // new frame
    if (LIKELY(frame != NULL)) {
        if (frame->name) {
            tableSet(&EC->roGlobals, OBJ_VAL(vm.funcString), OBJ_VAL(frame->name));
        } else {
            if (EC->frameCount == 1) {
                tableSet(&EC->roGlobals, OBJ_VAL(vm.funcString), OBJ_VAL(vm.mainString));
            } else {
                tableSet(&EC->roGlobals, OBJ_VAL(vm.funcString), OBJ_VAL(vm.anonString));
            }
        }
        if (stackAdjust > 0) {
            if (EC->stackTop-stackAdjust > EC->stack) {
                EC->stackTop -= stackAdjust;
            }
        }
    }
    ASSERT_VALID_STACK();
}

CallFrame *pushFrame(ObjFunction *userFunc) {
    DBG_ASSERT(vm.inited);
    register VMExecContext *ec = EC;
    if (UNLIKELY(ec->frameCount >= FRAMES_MAX)) {
        throwErrorFmt(lxErrClass, "Stackoverflow, max number of call frames (%d)", FRAMES_MAX);
        UNREACHABLE_RETURN(NULL);
    }
    CallFrame *prev = getFrameOrNull();
    CallFrame *frame = &ec->frames[ec->frameCount++];
    memset(frame, 0, sizeof(*frame));
    frame->callLine = curLine;
    frame->file = ec->filename;
    frame->prev = prev;
    BlockStackEntry *bentry = vec_last_or(&vm.curThread->v_blockStack, NULL);
    if (bentry && bentry->frame == NULL) {
        bentry->frame = frame;
    }
    if (userFunc) {
        frame->scope = newScope(userFunc);
    } else {
        frame->scope = NULL;
    }
    return frame;
}

const char *callFrameName(CallFrame *frame) {
    ObjString *fnName = frame->isCCall ? frame->nativeFunc->name :
      frame->closure->function->name;
    // TODO: fix, this could just be an anonymous function
    return fnName ? fnName->chars : "<main>";
}

static long newFrameCookie() {
  static long cookie = 0;
  return cookie++;
}

static void pushNativeFrame(ObjNative *native) {
    DBG_ASSERT(vm.inited);
    DBG_ASSERT(native);
    VM_DEBUG(2, "Pushing native callframe for %s", native->name->chars);
    if (UNLIKELY(EC->frameCount == FRAMES_MAX)) {
        errorPrintScriptBacktrace("Stack overflow.");
        return;
    }
    CallFrame *prevFrame = getFrame();
    CallFrame *newFrame = pushFrame(NULL);
    newFrame->closure = prevFrame->closure;
    newFrame->ip = prevFrame->ip;
    newFrame->start = 0;
    newFrame->slots = prevFrame->slots;
    newFrame->isCCall = true;
    newFrame->nativeFunc = native;
    newFrame->name = native->name;
    newFrame->file = EC->filename;
    newFrame->cookie = newFrameCookie();
    BlockStackEntry *bentry = vec_last_or(&vm.curThread->v_blockStack, NULL);
    if (bentry && bentry->frame == NULL) {
        bentry->frame = newFrame;
    }
    vm.curThread->inCCall++;
}

// sets up VM/C call jumpbuf if not set
static Value captureNativeError(ObjNative *nativeFunc, int argCount, Value *args, CallInfo *cinfo) {
    LxThread *th = vm.curThread;
    if (cinfo && cinfo->blockInstance) {
        DBG_ASSERT(argCount > 0); // at least the block argument
        argCount--; // block arg is implicit
    }
    if (th->inCCall == 0) {
        VM_DEBUG(2, "setting VM/C error jump buf");
        int jumpRes = setjmp(th->cCallJumpBuf);
        if (jumpRes == JUMP_SET) { // jump is set, prepared to enter C land
            th->cCallJumpBufSet = true;
            Value ret = nativeFunc->function(argCount, args);
            return ret;
        } else { // C call longjmped here from throwError()
            VM_DEBUG(2, "longmped to VM/C error jump buf");
            th = THREAD();
            ASSERT(th->inCCall > 0);
            ASSERT(th->cCallThrew);
            th->cCallThrew = false;
            th->returnedFromNativeErr = true;
            th->cCallJumpBufSet = false;
            return UNDEF_VAL;
        }
    } else {
        VM_DEBUG(2, "%s", "Calling native function");
        return nativeFunc->function(argCount, args);
    }
}

static inline bool checkFunctionArity(ObjFunction *func, int argCount, CallInfo *cinfo) {
    int arityMin = func->arity;
    int arityMax = arityMin + func->numDefaultArgs + func->numKwargs + (func->hasBlockArg ? 1 : 0);
    if (func->hasRestArg) arityMax = 20; // TODO: make a #define
    if (UNLIKELY(argCount < arityMin || argCount > arityMax)) {
        if (arityMin == arityMax) {
            throwArgErrorFmt("Expected %d arguments but got %d.",
                    arityMin, argCount);
        } else {
            throwArgErrorFmt("Expected %d-%d arguments but got %d.",
                    arityMin, arityMax, argCount);
        }
    }
    return true;
}

// TODO: fixme, this leaks memory right now. The scope is already allocated
// with a given size, we shouldn't reallocate. Once scopes are working I need to
// fix this.
static void setupLocalsTable(CallFrame *frame) {
    int localsSize = EC->stackTop - frame->slots;
    ASSERT(frame->scope);
    frame->scope->localsTable.size = localsSize;
    frame->scope->localsTable.capacity = localsSize;
    VM_DEBUG(1, "Setting up localsTable for frame %s, size %d", callFrameName(frame), localsSize);
    if (localsSize > 0) {
        frame->scope->localsTable.tbl = xmalloc(sizeof(Value) * localsSize);
        VM_DEBUG(2, "Setting up localsTable for frame %s, size %d", callFrameName(frame), localsSize);
        ASSERT_MEM(frame->scope->localsTable.tbl);
        memcpy(frame->scope->localsTable.tbl, frame->slots, sizeof(Value)*localsSize);
    }
}

void growLocalsTable(ObjScope *scope, int size) {
  if (scope->localsTable.capacity >= size) {
    scope->localsTable.size = size;
    return;
  }
  int max = size * 2;
  if (scope->localsTable.capacity == 0) {
    CallFrame *frame = getFrame();
    VM_DEBUG(2, "Growing localsTable (1) for frame %s, size %d, capa new %d, old capa: %d, old size: %d",
        callFrameName(frame), size, max, scope->localsTable.capacity, scope->localsTable.size);
    scope->localsTable.size = size;
    scope->localsTable.capacity = max;
    scope->localsTable.tbl = xmalloc(sizeof(Value)*max);
    ASSERT_MEM(scope->localsTable.tbl);
    nil_mem(scope->localsTable.tbl, max);
  } else {
    CallFrame *frame = getFrame();
    VM_DEBUG(2, "Growing localsTable (2) for frame %s, size %d, capa new %d, old capa: %d, old size: %d",
        callFrameName(frame), size, max, scope->localsTable.capacity, scope->localsTable.size);
    int old_capa = scope->localsTable.capacity;
    scope->localsTable.size = size;
    scope->localsTable.capacity = max;
    scope->localsTable.tbl = realloc(scope->localsTable.tbl, scope->localsTable.capacity*sizeof(Value));
    ASSERT_MEM(scope->localsTable.tbl);
    nil_mem(scope->localsTable.tbl+old_capa, max-old_capa);
  }
}

// Arguments are expected to be pushed on to stack by caller, including the
// callable object. Argcount does NOT include the instance argument, ex: a method with no arguments will have an
// argCount of 0. If the callable is a class (constructor), this function creates the
// new instance and puts it in the proper spot in the stack. The return value
// is pushed to the stack.
static bool doCallCallable(Value callable, int argCount, bool isMethod, CallInfo *callInfo) {
    LxThread *th = vm.curThread;
    Value blockInstance = NIL_VAL;
    bool blockInstancePopped = false;
    if (argCount > 0 && callInfo) {
        if (IS_A_BLOCK(peek(0))) {
            Value blkObj = peek(0);
            callInfo->blockInstance = AS_INSTANCE(blkObj);
            blockInstance = blkObj;
        }
    }
    if (callInfo && callInfo->blockInstance && !IS_A_BLOCK(peek(0))) {
        push(OBJ_VAL(callInfo->blockInstance));
        blockInstance = peek(0);
        argCount++;
    }

    ObjClosure *closure = NULL;
    Value instanceVal;
    volatile ObjInstance *instance = NULL;
    ObjClass *frameClass = NULL;
    if (isMethod) {
        instance = AS_INSTANCE(EC->stackTop[-argCount-1]);
        if (UNLIKELY(!isInstanceLikeObj(TO_OBJ(instance)))) {
            throwErrorFmt(lxTypeErrClass, "Tried to invoke method '%s' on non-instance (type=%s)", getCallableFunctionName(callable)->chars, typeOfObj(TO_OBJ(instance)));
        }
        frameClass = instance->klass; // TODO: make class the callable's class, not the instance class
    } else {
        // callable should be on stack, below the arguments
        ASSERT(isCallable(*(EC->stackTop-argCount-1))); // should be same as `callable` param
    }
    if (IS_CLOSURE(callable)) { // lox function
        closure = AS_CLOSURE(callable);
        if (!isMethod) {
            EC->stackTop[-argCount - 1] = callable; // should already be the callable, but just in case
        }
    } else if (IS_CLASS(callable)) { // initializer
        ObjClass *klass = AS_CLASS(callable);
        const char *klassName = NULL;
        (void)klassName;
#ifndef NDEBUG
        klassName = className(klass);
#endif
        VM_DEBUG(2, "calling callable class %s", klassName);
        instance = newInstance(klass, NEWOBJ_FLAG_NONE); // setup the new instance object
        frameClass = klass;
        instanceVal = OBJ_VAL(instance);
        th->thisObj = TO_OBJ(instance); // to avoid GC
        /*ASSERT(IS_CLASS(EC->stackTop[-argCount - 1])); this holds true if the # of args is correct for the function */
        EC->stackTop[-argCount - 1] = instanceVal; // first argument is instance, replaces class object
        // Call the initializer, if there is one.
        Value initializer;
        Obj *init = instanceFindMethod((ObjInstance*)instance, vm.initString);
        isMethod = true;
        if (init) {
            VM_DEBUG(2, "callable is initializer for class %s", klassName);
            initializer = OBJ_VAL(init);
            if (IS_NATIVE_FUNCTION(initializer)) {
                ObjNative *nativeInit = AS_NATIVE_FUNCTION(initializer);
                ASSERT(nativeInit->function);
                pushNativeFrame(nativeInit);
                volatile CallFrame *newFrame = getFrame();
                DBG_ASSERT(instance);
                newFrame->instance = TO_INSTANCE(instance);
                vec_push(&th->v_thisStack, TO_OBJ(instance));
                th->thisObj = TO_OBJ(instance);
                newFrame->klass = frameClass;
                if (newFrame->klass) {
                    pushCref(newFrame->klass);
                }
                newFrame->callInfo = callInfo;
                VM_DEBUG(2, "calling native initializer for class %s with %d args", klassName, argCount);
                Value val = captureNativeError(nativeInit, argCount+1, EC->stackTop-argCount-1, callInfo);
                th = vm.curThread;
                newFrame->slots = EC->stackTop-argCount-1;
                if (UNLIKELY(th->returnedFromNativeErr)) {
                    th->returnedFromNativeErr = false;
                    VM_DEBUG(2, "native initializer returned from error");
                    vec_clear(&th->stackObjects);
                    while (getFrame() >= newFrame) {
                        popFrame();
                    }
                    ASSERT(th->inCCall == 0);
                    VM_DEBUG(2, "Rethrowing inside VM");
                    throwError(th->lastErrorThrown); // re-throw inside VM
                    return false;
                } else {
                    VM_DEBUG(2, "native initializer returned");
                    EC->stackTop = getFrame()->slots;
                    popFrame();
                    ASSERT(IS_INSTANCE_LIKE(val));
                    push(val);
                    return true;
                }
            }
            VM_DEBUG(2, "calling non-native initializer with %d args", argCount);
            ASSERT(IS_CLOSURE(initializer));
            closure = AS_CLOSURE(initializer);
        } else {
            throwArgErrorFmt("init() method not found?", argCount);
        }
    } else if (IS_BOUND_METHOD(callable)) {
        VM_DEBUG(2, "calling bound method with %d args", argCount);
        ObjBoundMethod *bmethod = AS_BOUND_METHOD(callable);
        Obj *callable = bmethod->callable; // native function or user-defined function (ObjClosure)
        instanceVal = bmethod->receiver;
        EC->stackTop[-argCount - 1] = instanceVal;
        return doCallCallable(OBJ_VAL(callable), argCount, true, callInfo);
    } else if (IS_NATIVE_FUNCTION(callable)) {
#ifndef NDEBUG
        if (GET_OPTION(debugVMLvl) >= 2) {
            char callableNameBuf[200];
            fillCallableName(callable, callableNameBuf, 200);
            VM_DEBUG(2, "Calling native %s %s with %d args", isMethod ? "method" : "function", callableNameBuf, argCount);
        }
#endif
        volatile ObjNative *native = AS_NATIVE_FUNCTION(callable);
        int argCountActual = argCount; // includes the callable on the stack, or the receiver if it's a method
        if (isMethod) {
            argCount++;
            argCountActual++;
            if (!instance) {
                instance = AS_INSTANCE(*(EC->stackTop-argCount));
                DBG_ASSERT(TO_OBJ(instance)->type == OBJ_T_INSTANCE);
                frameClass = instance->klass;
            }
        } else {
            argCountActual++;
        }
        volatile int argci = argCount;
        volatile int argcActuali = argCountActual;
        pushNativeFrame(TO_NATIVE(native));
        volatile CallFrame *newFrame = getFrame();
        volatile VMExecContext *ec = EC;
        newFrame->instance = TO_INSTANCE(instance); // NOTE: can be NULL, if not method
        if (newFrame->instance) {
            th->thisObj = TO_OBJ(instance);
            vec_push(&th->v_thisStack, TO_INSTANCE(instance));
        }
        newFrame->klass = frameClass;
        if (newFrame->klass) {
            pushCref(newFrame->klass);
        }
        newFrame->callInfo = callInfo;
        volatile Value val = captureNativeError(TO_NATIVE(native), argci, EC->stackTop-argci, callInfo);
        th = vm.curThread;
        newFrame->slots = ec->stackTop - argcActuali;
        if (th->returnedFromNativeErr) {
            VM_DEBUG(2, "Returned from native function with error");
            th->returnedFromNativeErr = false;
            while (getFrame() >= newFrame) {
                popFrame();
            }
            th->inCCall = 0;
            VM_DEBUG(2, "Rethrowing inside VM");
            throwError(th->lastErrorThrown); // re-throw inside VM
            return false;
        } else {
            VM_DEBUG(2, "Returned from native function without error");
            ec->stackTop = getFrame()->slots;
            popFrame();
            ASSERT(!IS_UNDEF(val));
            push(val);
        }
        return true;
    } else {
        UNREACHABLE("bad callable value given to callCallable: %s", typeOfVal(callable));
    }

    if (UNLIKELY(EC->frameCount >= FRAMES_MAX)) {
        errorPrintScriptBacktrace("Stack overflow.");
        return false;
    }

    VM_DEBUG(2, "doCallCallable found closure");
    // non-native function/method (defined in lox code)
    ASSERT(closure);
    ObjFunction *func = closure->function;
    if (closure->isBlock || (callInfo && callInfo->blockInstance)) {
        // no arity check
    } else {
        checkFunctionArity(func, argCount, callInfo);
    }

    vec_nodep_t *params = (vec_nodep_t*)nodeGetData(func->funcNode);
    ASSERT(params);

    Value kwargsMap = NIL_VAL;
    // keyword arg processing
    if (func->numKwargs > 0 && callInfo) {
        if (!IS_NIL(blockInstance) && IS_A_BLOCK(peek(0))) {
            argCount--;
            pop();
            blockInstancePopped = true;
        }
        kwargsMap = newMap();
        Node *param = NULL;
        int pi = 0;
        vec_foreach_rev(params, param, pi) {
            if (param->type.kind == PARAM_NODE_KWARG) {
                char *kwname = tokStr(&param->tok);
                ObjString *kwStr = INTERN(kwname);
                for (int i = 0; i < callInfo->numKwargs; i++) {
                    // keyword argument given, is on stack, we pop it off
                    if (strcmp(kwname, tokStr(callInfo->kwargNames+i)) == 0) {
                        mapSet(kwargsMap, OBJ_VAL(kwStr), pop());
                    }
                }
                // when keyword argument not given, we need to add UNDEF_VAL to
                // stack later
            }
        }
    }

    if (func->numDefaultArgs > 0 && !IS_NIL(blockInstance) && !blockInstancePopped && IS_A_BLOCK(peek(0))) {
        pop();
        argCount--;
        blockInstancePopped = true;
    }

    // default arg processing
    int numDefaultArgsUsed = (func->arity + func->numDefaultArgs)-argCount;
    if (numDefaultArgsUsed < 0) numDefaultArgsUsed = 0;
    int numDefaultArgsUnused = func->numDefaultArgs - numDefaultArgsUsed;

    for (int i = 0; i < numDefaultArgsUsed; i++) {
        push(NIL_VAL);
    }

    // rest argument processing (splats)
    bool hasRestArg = func->hasRestArg;
    int numRestArgs = 0;
    int argCountWithRestAry = argCount;
    if (hasRestArg && argCount > (func->arity + func->numDefaultArgs)) {
        numRestArgs = argCount - (func->arity + func->numDefaultArgs);
        if (numRestArgs > 0) {
            Value restAry = newArray();
            for (int i = numRestArgs; i > 0; i--) {
                Value arg = peek(i-1);
                arrayPush(restAry, arg);
                argCountWithRestAry--;
            }
            for (int i = numRestArgs; i > 0; i--) { pop(); }
            push(restAry);
            argCountWithRestAry++;
        } else {
            ASSERT(0);
        }
    // empty rest arg
    } else if (hasRestArg) {
        Value restAry = newArray();
        push(restAry);
        argCountWithRestAry++;
    }

    int numKwargsNotGiven = 0;
    if (func->numKwargs > 0 && callInfo) {
        Node *param = NULL;
        int pi = 0;
        vec_foreach(params, param, pi) {
            if (param->type.kind == PARAM_NODE_KWARG) {
                char *kwname = tokStr(&param->tok);
                ObjString *kwStr = INTERN(kwname);
                Value val;
                if (MAP_GET(kwargsMap, OBJ_VAL(kwStr), &val)) {
                    push(val);
                } else {
                    push(UNDEF_VAL);
                    numKwargsNotGiven++;
                }
            }
        }
    }

    CallFrame *f = getFrame();
    Chunk *ch = f->closure->function->chunk;
    int parentStart = f->ip - ch->code - 2;
    ASSERT(parentStart >= 0);

    size_t funcOffset = 0;
    VM_DEBUG(2,
        "arity: %d, defaultArgs: %d, defaultsUsed: %d\n"
        "defaultsUnused: %d, numRestArgs: %d, argCount: %d\n"
        "kwargsAvail: %d, kwargsGiven: %d",
        func->arity, func->numDefaultArgs, numDefaultArgsUsed,
        numDefaultArgsUnused, numRestArgs, argCount,
        func->numKwargs, func->numKwargs-numKwargsNotGiven
    );

    // skip default argument code in function that's unused
    if (numDefaultArgsUnused > 0) {
        ASSERT(func->funcNode);
        Node *param = NULL;
        int pi = 0;
        int unused = numDefaultArgsUnused;
        vec_foreach_rev(params, param, pi) {
            if (param->type.kind == PARAM_NODE_SPLAT) continue;
            if (param->type.kind == PARAM_NODE_BLOCK) continue;
            if (param->type.kind == PARAM_NODE_DEFAULT_ARG) {
                size_t offset = ((ParamNodeInfo*)param->data)->defaultArgIPOffset;
                VM_DEBUG(2, "default param found: offset=%d", (int)offset);
                funcOffset += offset;
                unused--;
                if (unused == 0) break;
            } else {
                ASSERT(0); // unreachable, default args should be last args, not including splat or block arg
                break;
            }
        }
    }

    if (func->hasBlockArg && callInfo->blockFunction && IS_NIL(blockInstance)) {
        // TODO: get closure created here with upvals!
        Value blockClosure = OBJ_VAL(newClosure(callInfo->blockFunction, NEWOBJ_FLAG_NONE));
        Obj *blkClosure = AS_OBJ(blockClosure);
        push(newBlock(blkClosure));
        argCountWithRestAry++;
    } else if (func->hasBlockArg && IS_NIL(blockInstance)) {
        push(NIL_VAL);
        argCountWithRestAry++;
    }

    if (blockInstancePopped) {
        push(blockInstance);
        argCountWithRestAry++;
    }

    if (func->numKwargs > 0) {
        push(kwargsMap);
    }

    // add frame
    VM_DEBUG(2, "%s", "Pushing callframe (non-native)");
    CallFrame *frame = pushFrame(func);
    frame->instance = TO_INSTANCE(instance);
    if (instance) {
        th->thisObj = TO_OBJ(instance);
        vec_push(&vm.curThread->v_thisStack, TO_OBJ(instance));
        if (IS_T_CLASS(instance) || IS_T_MODULE(instance)) {
            frameClass = TO_CLASS(instance);
        }
    }
    frame->callInfo = callInfo;
    if (instance && !frameClass) {
        frameClass = instance->klass;
    }
    frame->klass = frameClass;
    if (frame->klass) {
        pushCref(frame->klass);
    }
    if (funcOffset > 0) {
        VM_DEBUG(2, "Func offset due to optargs: %d", (int)funcOffset);
    }
    frame->closure = closure;
    frame->name = closure->function->name;
    frame->ip = closure->function->chunk->code + funcOffset;
    frame->start = parentStart;
    frame->isCCall = false;
    frame->nativeFunc = NULL;
    if (callInfo && callInfo->isYield) {
        frame->stackAdjustOnPop = (callInfo->argc+1);
    }
    // +1 to include either the called function (for non-methods) or the receiver (for methods)
    frame->slots = EC->stackTop - (argCountWithRestAry + numDefaultArgsUsed + 1) -
        (func->numKwargs > 0 ? numKwargsNotGiven+1 : 0);
    setupLocalsTable(frame);
    if (frame->name) {
        tableSet(&EC->roGlobals, OBJ_VAL(vm.funcString), OBJ_VAL(frame->name));
    } else {
        tableSet(&EC->roGlobals, OBJ_VAL(vm.funcString), OBJ_VAL(vm.anonString));
    }
    // NOTE: the frame is popped on OP_RETURN or non-local jump
    vm_run(); // actually run the function until return
    return true;
}


/**
 * see doCallCallable
 * argCount does NOT include the instance if `isMethod` is true
 */
bool callCallable(Value callable, int argCount, bool isMethod, CallInfo *info) {
    DBG_ASSERT(vm.inited);
    LxThread *th = vm.curThread;
    int lenBefore = th->stackObjects.length;
    bool ret = doCallCallable(callable, argCount, isMethod, info);

    // allow collection of new stack-created objects if they're not rooted now
    th->stackObjects.length = lenBefore;

    return ret;
}

Obj *findMethod(Obj *klass, ObjString *methodName) {
    Value method;
    Obj *classLookup = klass;
    while (classLookup) {
        Table *mtable = CLASS_METHOD_TBL(classLookup);
        if (tableGet(mtable, OBJ_VAL(methodName), &method)) {
            return AS_OBJ(method);
        }
        classLookup = CLASS_SUPER(classLookup);
    }
    return NULL;
}

// API for calling 'super' in native C methods
Value callSuper(int argCount, Value *args, CallInfo *cinfo) {
    if (UNLIKELY(!isClassHierarchyCreated)) return NIL_VAL;
    CallFrame *frame = getFrame();
    DBG_ASSERT(frame->instance);
    DBG_ASSERT(frame->klass); // TODO: maybe throw error if no class here?
    if (UNLIKELY(!frame->isCCall)) {
        throwErrorFmt(lxErrClass, "callSuper must be called from native C function!");
    }
    ObjNative *method = frame->nativeFunc;
    DBG_ASSERT(method);
    Obj *klass = method->klass;
    if (UNLIKELY(!klass)) {
        throwErrorFmt(lxErrClass, "No class found for callSuper, current frame must be a method!");
    }
    ObjString *methodName = method->name;
    DBG_ASSERT(methodName);
    if (LIKELY(klass->type == OBJ_T_CLASS || klass->type == OBJ_T_MODULE)) {
        if ((ObjClass*)klass == lxObjClass) { // no super
            return NIL_VAL;
        }
        Obj *superClass = CLASS_SUPER(klass);
        if (superClass == TO_OBJ(lxObjClass)) {
            superClass->type = OBJ_T_CLASS; // FIXME: not sure why needed, but when stressing GC with --stress-GC=young, some scripts fails without this!
        }
        if (UNLIKELY(!superClass)) {
            throwErrorFmt(lxErrClass, "No superclass found for callSuper");
        }
        Obj *superMethod = findMethod(superClass, methodName);
        if (UNLIKELY(!superMethod)) {
            throwErrorFmt(lxErrClass, "No super method found for callSuper");
        }
        callVMMethod(frame->instance, OBJ_VAL(superMethod), argCount, args, cinfo);
        return pop();
    } else {
        UNREACHABLE("bug: class type: %s", objTypeName(klass->type));
    }
    return NIL_VAL;
}

/**
 * When thrown (OP_THROW), find any surrounding try { } catch { } block with
 * the proper class to catch.
 */
static bool findThrowJumpLoc(ObjClass *klass, uint8_t **ipOut, CatchTable **rowFound) {
    LxThread *th = vm.curThread;
    CatchTable *tbl = currentChunk()->catchTbl;
    CatchTable *row = tbl;
    int currentIpOff = (int)(getFrame()->ip - currentChunk()->code);
    bool poppedEC = false;
    VM_DEBUG(2, "findthrowjumploc");
    CatchTable *ensureRow = NULL;
    while (row || EC->frameCount >= 1) {
        VM_DEBUG(2, "framecount: %d, num ECs: %d", EC->frameCount, th->v_ecs.length);
        if (row == NULL) { // no more catch table rows, pop a call frame
            VM_DEBUG(2, "row null");
            if (ensureRow) {
                if (ensureRow->isEnsureRunning) {
                    ensureRow->isEnsureRunning = false;
                } else {
                  // found target ensure
                  *ipOut = currentChunk()->code + ensureRow->itarget;
                  *rowFound = ensureRow;
                  while (getFrame()->isCCall) {
                    popFrame();
                  }
                  ensureRow->isEnsureRunning = true;
                  VM_DEBUG(2, "Catch jump location found (ensure)");
                  return true;
                }
            }
            if (th->v_ecs.length == 0 || (th->v_ecs.length == 1 && EC->frameCount == 1)) {
                return false;
            }
            if (EC->frameCount == 1) { // there's at least 1 more context to go through
                pop_EC();
                poppedEC = true;
                ASSERT(EC->stackTop > getFrame()->slots);
                row = currentChunk()->catchTbl;
                ensureRow = NULL;
                continue;
            } else { // more frames in this context to go through
                ASSERT(EC->frameCount > 1);
                currentIpOff = getFrame()->start;
                ASSERT(EC->stackTop >= getFrame()->slots);
                EC->stackTop = getFrame()->slots;
                popFrame();
                VM_DEBUG(2, "frame popped");
                row = currentChunk()->catchTbl;
                ensureRow = NULL;
                continue;
            }
        }
        // jump to location in catch row, if catch found
        if (row->isEnsure) {
            ensureRow = row;
            row = row->next;
            continue;
        }
        Value klassFound;
        ObjClass *cref = NULL;
        if (vm.curThread->v_crefStack.length > 0) {
            cref = (ObjClass*)vec_last(&vm.curThread->v_crefStack);
        }
        if (!findConstantUnder(cref, AS_STRING(row->catchVal), &klassFound)) {
            VM_DEBUG(2, "a class not found for row, next row");
            row = row->next;
            continue;
        }
        VM_DEBUG(2, "a class found for row: %s", classNameFull(AS_CLASS(klassFound))->chars);
        if (IS_SUBCLASS(klass, AS_CLASS(klassFound))) {
            VM_DEBUG(2, "good class found for row");
            if (poppedEC || (currentIpOff > row->ifrom && currentIpOff <= row->ito)) {
                // found target catch
                *ipOut = currentChunk()->code + row->itarget;
                *rowFound = row;
                while (getFrame()->isCCall) {
                    popFrame();
                }
                VM_DEBUG(2, "Catch jump location found");
                return true;
            }
        }
        row = row->next;
    }

    VM_DEBUG(2, "Catch jump location NOT found");
    return false;
}

static CatchTable *getCatchTableRow(int idx) {
    CatchTable *tbl = currentChunk()->catchTbl;
    CatchTable *row = tbl;
    int i = 0;
    while (i < idx) {
        ASSERT(row);
        ASSERT(row->next);
        row = row->next;
        i++;
    }
    ASSERT(row);
    return row;
}

static ErrTagInfo *findErrTag(ObjClass *klass) {
    ASSERT(klass);
    ErrTagInfo *cur = vm.curThread->errInfo;
    while (cur) {
        // NULL = tag for all errors
        if (cur->errClass == NULL || IS_SUBCLASS(klass, cur->errClass)) {
            return cur;
        }
        cur = cur->prev;
    }
    return NULL;
}

NORETURN void throwError(Value self) {
    VM_DEBUG(2, "throwing error");
    ASSERT(vm.inited);
    ASSERT(IS_INSTANCE(self));
    LxThread *th = vm.curThread;
    th->lastErrorThrown = self;
    if (IS_NIL(getProp(self, INTERNED("backtrace", 9)))) {
        setBacktrace(self);
    }
    // error from VM
    ObjInstance *obj = AS_INSTANCE(self);
    ObjClass *klass = obj->klass;
    CatchTable *catchRow;
    uint8_t *ipNew = NULL;
    ErrTagInfo *errInfo = NULL;
    if ((errInfo = findErrTag(klass))) {
        VM_DEBUG(2, "longjmping to errinfo tag");
        longjmp(errInfo->jmpBuf, JUMP_PERFORMED);
    }
    if (th->inCCall > 0 && th->cCallJumpBufSet && getFrame()->isCCall) {
        VM_DEBUG(2, "throwing error from C call, longjmping");
        ASSERT(!th->cCallThrew);
        th->cCallThrew = true;
        longjmp(th->cCallJumpBuf, JUMP_PERFORMED);
    }
    if (findThrowJumpLoc(klass, &ipNew, &catchRow)) {
        ASSERT(ipNew);
        ASSERT(catchRow);
        catchRow->lastThrownValue = self;
        getFrame()->ip = ipNew;
        DBG_ASSERT(getFrame()->closure->function->chunk->catchTbl);
        DBG_ASSERT(getFrame()->jmpBufSet);
        longjmp(getFrame()->jmpBuf, JUMP_PERFORMED);
    } else {
        ASSERT(rootVMLoopJumpBufSet);
        longjmp(rootVMLoopJumpBuf, JUMP_PERFORMED);
    }
    UNREACHABLE("after longjmp");
}

void popErrInfo(void) {
    DBG_ASSERT(vm.curThread->errInfo);
    vm.curThread->errInfo = vm.curThread->errInfo->prev;
}

void unsetErrInfo(void) {
    LxThread *th = vm.curThread;
    th->lastErrorThrown = NIL_VAL;
    ASSERT(th->errInfo);
    th->errInfo = th->errInfo->prev;
}

NORETURN void rethrowErrInfo(ErrTagInfo *info) {
    ASSERT(info);
    Value err = info->caughtError;
    popErrInfo();
    throwError(err);
}

NORETURN void throwErrorFmt(ObjClass *klass, const char *format, ...) {
    char sbuf[250];
    va_list args;
    va_start(args, format);
    vsnprintf(sbuf, 250, format, args);
    va_end(args);
    size_t len = strlen(sbuf);
    char *cbuf = ALLOCATE(char, len+1); // uses takeString below
    strncpy(cbuf, sbuf, len);
    cbuf[len] = '\0';
    ObjString *buf = takeString(cbuf, len, NEWOBJ_FLAG_NONE);
    hideFromGC(TO_OBJ(buf));
    Value msg = OBJ_VAL(buf);
    Value err = newError(klass, msg);
    vm.curThread->lastErrorThrown = err;
    unhideFromGC(TO_OBJ(buf));
    throwError(err);
    UNREACHABLE("thrown");
}

void printVMStack(FILE *f, LxThread *th) {
    pthread_t tid = 0;
    if (th != vm.mainThread) {
        tid = th->tid;
    }
    if (th->ec->stackTop == th->ec->stack && th->v_ecs.length == 1) {
        fprintf(f, "[DEBUG %d (th=%lu)]: Stack: empty\n", th->vmRunLvl, (unsigned long)tid);
        return;
    }
    VMExecContext *ec = NULL; int i = 0;
    int numCallFrames = VMNumCallFrames();
    int numStackFrames = VMNumStackFrames();
    fprintf(f, "[DEBUG %d (th=%lu)]: Stack (%d stack frames, %d call frames):\n", th->vmRunLvl,
            (unsigned long)tid, numStackFrames, numCallFrames);
    // print VM stack values from bottom of stack to top
    fprintf(f, "[DEBUG %d]: ", th->vmRunLvl);
    int callFrameIdx = 0;
    vec_foreach(&th->v_ecs, ec, i) {
        for (Value *slot = ec->stack; slot < ec->stackTop; slot++) {
            if (IS_OBJ(*slot) && (AS_OBJ(*slot)->type <= OBJ_T_NONE ||
                                 (AS_OBJ(*slot)->type >= OBJ_T_LAST))) {
                fprintf(stderr, "[DEBUG %d]: Broken object pointer: %p\n", th->vmRunLvl,
                        AS_OBJ(*slot));
                ASSERT(0);
            }
            if (ec->frames[callFrameIdx].slots == slot) {
                fprintf(f, "(CF %d)", callFrameIdx+1);
                callFrameIdx++;
            }
            fprintf(f, "[ ");
            if (printValue(f, *slot, false, 20) == 20) {
                fprintf(f, "(...)");
            }
            fprintf(f, " ]");
            if (IS_OBJ(*slot)) {
                Obj *objPtr = AS_OBJ(*slot);
                if (OBJ_IS_HIDDEN(objPtr)) {
                    fprintf(f, " (hidden!)");
                }
            }
        }
    }
    fprintf(f, "\n");
}

ObjUpvalue *captureUpvalue(Value *local) {
    LxThread *th = vm.curThread;
    ObjInstance *threadInst = FIND_THREAD_INSTANCE(th->tid);
    ASSERT(threadInst);
    if (th->openUpvalues == NULL) {
        th->openUpvalues = newUpvalue(local, NEWOBJ_FLAG_NONE);
        OBJ_WRITE(OBJ_VAL(threadInst), OBJ_VAL(th->openUpvalues));
        return th->openUpvalues;
    }

    if (GET_OPTION(debugVMLvl) >= 2) {
        VM_DEBUG(2, "Capturing upvalue: ");
        printValue(stderr, *local, false, -1);
        fprintf(stderr, "\n");
    }

    ObjUpvalue *prevUpvalue = NULL;
    ObjUpvalue *upvalue = th->openUpvalues;
    // th->openUpvalues is a linked list of upvalue objects, top of stack are in the list first

    // Walk towards the bottom of the stack until we find a previously existing
    // upvalue or reach where it should be.
    while (upvalue != NULL && upvalue->value > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    // If we found it, reuse it.
    if (upvalue != NULL && upvalue->value == local) return upvalue;

    // We walked past the local on the stack, so there must not be an upvalue for
    // it already. Make a new one and link it in in the right place to keep the
    // list sorted.
    ObjUpvalue *createdUpvalue = newUpvalue(local, NEWOBJ_FLAG_NONE);
    OBJ_WRITE(OBJ_VAL(threadInst), OBJ_VAL(createdUpvalue));
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        // The new one is the first one in the list.
        th->openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

// Close the upvalues that are going to get popped off the stack
// param `last` is the stackTop after popFrame()
static void closeUpvalues(Value *last) {
    LxThread *th = vm.curThread;
    while (th->openUpvalues != NULL && th->openUpvalues->value >= last) {
        ObjUpvalue *upvalue = th->openUpvalues;

        // Copy the value into the upvalue itself and point the upvalue to it.
        upvalue->closed = *upvalue->value;
        upvalue->value = &upvalue->closed;

        // Pop it off the open upvalue list.
        th->openUpvalues = upvalue->next;
    }
}

static Value unpackValue(Value val, uint8_t idx) {
    if (IS_AN_ARRAY(val)) {
        if (idx < ARRAY_SIZE(val)) {
            return ARRAY_GET(val, idx);
        } else {
            return NIL_VAL;
        }
    } else {
        fprintf(stderr, "[BUG]: type: %s\n", typeOfVal(val));
        UNREACHABLE("type: %s", typeOfVal(val)); // FIXME: throw typeerror
    }
}

static ObjString *methodNameForBinop(OpCode code) {
    switch (code) {
    case OP_ADD:
        return vm.opAddString;
    case OP_SUBTRACT:
        return vm.opDiffString;
    case OP_MULTIPLY:
        return vm.opMulString;
    case OP_DIVIDE:
        return vm.opDivString;
    case OP_SHOVEL_L:
        return vm.opShovelLString;
    case OP_SHOVEL_R:
        return vm.opShovelRString;
    default:
        return NULL;
    }
}

static InterpretResult vm_run0() {
    if (CLOX_OPTION_T(parseOnly) || CLOX_OPTION_T(compileOnly)) {
        return INTERPRET_OK;
    }

    if (!rootVMLoopJumpBufSet) {
        ASSERT(vm.curThread->vmRunLvl == 0);
        int jumpRes = setjmp(rootVMLoopJumpBuf);
        rootVMLoopJumpBufSet = true;
        if (jumpRes == JUMP_SET) {
            VM_DEBUG(1, "VM set rootVMLoopJumpBuf");
        } else {
            VM_DEBUG(1, "VM caught error in rootVMLoopJumpBuf");
            showUncaughtError(THREAD()->lastErrorThrown);
            return INTERPRET_RUNTIME_ERROR;
        }
    }
    return vm_run();
}

/**
 * Run the VM's instructions.
 */
static InterpretResult vm_run() {
    register LxThread *th = vm.curThread;
    register Chunk *ch = currentChunk();
    register Value *constantSlots = ch->constants->values;
    register CallFrame *frame = getFrame();
    ObjScope *scope = frame->scope;
    register VMExecContext *ctx = EC;
    th->vmRunLvl++;
    if (ch->catchTbl != NULL) {
        int jumpRes = setjmp(frame->jmpBuf);
        if (jumpRes == JUMP_SET) {
            frame->jmpBufSet = true;
            VM_DEBUG(2, "VM set catch table for call frame (vm_run lvl %d)", th->vmRunLvl-1);
        } else {
            VM_DEBUG(2, "VM caught error for call frame (vm_run lvl %d)", th->vmRunLvl-1);
            th = THREAD(); // clobbered
            th->hadError = false;
            ch = currentChunk(); // clobbered
            constantSlots = ch->constants->values; // clobbered
            frame = getFrame(); // clobbered
            // stack is already unwound to proper frame
        }
    }
#define READ_BYTE() (*(frame->ip++))
#define READ_CONSTANT() (constantSlots[READ_BYTE()])
#define BINARY_OP(op, opcode, type) \
    do { \
      Value b = VM_PEEK(0);\
      Value a = VM_PEEK(1);\
      if (IS_NUMBER(a) && IS_NUMBER(b)) {\
          if (UNLIKELY(((opcode == OP_DIVIDE || opcode == OP_MODULO) && AS_NUMBER(b) == 0.00))) {\
              throwErrorFmt(lxErrClass, "Can't divide by 0");\
          }\
          VM_POP();\
          VM_PUSHSWAP(NUMBER_VAL((type)AS_NUMBER(a) op (type)AS_NUMBER(b)));\
      } else if (IS_INSTANCE_LIKE(a)) {\
          ObjInstance *inst = AS_INSTANCE(a);\
          ObjString *methodName = methodNameForBinop(opcode);\
          Obj *callable = NULL;\
          if (methodName) {\
            callable = instanceFindMethod(inst, methodName);\
          }\
          if (UNLIKELY(!callable)) {\
              throwErrorFmt(lxNameErrClass, "Method %s#%s not found for operation '%s'", className(inst->klass), methodName->chars, #op);\
          }\
          callCallable(OBJ_VAL(callable), 1, true, NULL);\
      } else {\
          throwErrorFmt(lxTypeErrClass, "Binary operation type error, op=%s, lhs=%s, rhs=%s", #op, typeOfVal(a), typeOfVal(b));\
      }\
    } while (0)

  /*fprintf(stderr, "VM run level: %d\n", vmRunLvl);*/
  /* Main vm loop */
vmLoop:
      th->opsRemaining--;
      if (UNLIKELY(th->opsRemaining <= 0)) {
          th->opsRemaining = THREAD_OPS_UNTIL_SWITCH;
          if (!isOnlyThread()) {
              THREAD_DEBUG(5, "Releasing GVL after ops up %lu", pthread_self());
              releaseGVL(THREAD_STOPPED);
              threadSleepNano(th, 100);
              acquireGVL();
          } else {
              THREAD_DEBUG(5, "Skipped releasing GVL after ops up %lu", pthread_self());
          }
      }
      if (UNLIKELY(th->hadError)) {
          (th->vmRunLvl)--;
          return INTERPRET_RUNTIME_ERROR;
      }
      if (UNLIKELY(vm.exited)) {
          (th->vmRunLvl)--;
          return INTERPRET_OK;
      }
      if (UNLIKELY((EC->stackTop < EC->stack))) {
          ASSERT(0);
      }

      int byteCount = (int)(frame->ip - ch->code);
      curLine = ch->lines[byteCount];
      int lastLine = -1;
      int ndepth = ch->ndepths[byteCount];
      int nwidth = ch->nwidths[byteCount];
      /*fprintf(stderr, "line: %d, depth: %d, width: %d\n", curLine, ndepth, nwidth);*/
      if (byteCount > 0) {
          lastLine = ch->lines[byteCount-1];
      }
      if (UNLIKELY(curLine != lastLine && shouldEnterDebugger(&vm.debugger, "", curLine, lastLine, ndepth, nwidth))) {
          enterDebugger(&vm.debugger, "", curLine, ndepth, nwidth);
      }

#ifndef NDEBUG
    if (CLOX_OPTION_T(traceVMExecution)) {
        printVMStack(stderr, th);
        printDisassembledInstruction(stderr, ch, (int)(frame->ip - ch->code), NULL);
    }
#endif

#ifndef NDEBUG
    char *stepLine = NULL;
    size_t stepLineSz = 0;
    // interactive VM instruction stepper
    if (CLOX_OPTION_T(stepVMExecution) && vm.instructionStepperOn) {
        fprintf(stderr, "STEPPER> ");
        while (getline(&stepLine, &stepLineSz, stdin)) {
            if (strcmp("c\n", stepLine) == 0) {
                vm.instructionStepperOn = false;
                xfree(stepLine);
                break;
            } else if (strcmp("n\n", stepLine) == 0) {
                xfree(stepLine);
                break;
            } else {
                fprintf(stderr, "Unknown command!\n");
            }
        }
    }
#endif

#ifdef COMPUTED_GOTO
    static void *dispatchTable[] = {
    #define OPCODE(name) &&code_##name,
    #include "opcodes.h.inc"
    #undef OPCODE
    };
    #define CASE_OP(name)     code_##name

    #define DISPATCH_TOP      goto *dispatchTable[instruction];
    #define DISPATCH_BOTTOM() goto vmLoop
#else
    #define CASE_OP(name)     case OP_##name
    #define DISPATCH_BOTTOM() goto vmLoop
#endif

    uint8_t instruction = READ_BYTE();
#ifndef NDEBUG
    th->lastOp = instruction;
#endif

#ifdef COMPUTED_GOTO
    DISPATCH_TOP
#else
    switch (instruction) {
#endif
      CASE_OP(CONSTANT): { // numbers, code chunks (ObjFunction)
          Value constant = READ_CONSTANT();
          VM_PUSH(constant);
          DISPATCH_BOTTOM();
      }
      CASE_OP(ADD):      BINARY_OP(+,OP_ADD, double); DISPATCH_BOTTOM();
      CASE_OP(SUBTRACT): BINARY_OP(-,OP_SUBTRACT, double); DISPATCH_BOTTOM();
      CASE_OP(MULTIPLY): BINARY_OP(*,OP_MULTIPLY, double); DISPATCH_BOTTOM();
      CASE_OP(DIVIDE):   BINARY_OP(/,OP_DIVIDE, double); DISPATCH_BOTTOM();
      CASE_OP(MODULO):   BINARY_OP(%,OP_MODULO, int); DISPATCH_BOTTOM();
      CASE_OP(BITOR):    BINARY_OP(|,OP_BITOR, int); DISPATCH_BOTTOM();
      CASE_OP(BITAND):   BINARY_OP(&,OP_BITAND, int); DISPATCH_BOTTOM();
      CASE_OP(BITXOR):   BINARY_OP(^,OP_BITXOR, int); DISPATCH_BOTTOM();
      CASE_OP(SHOVEL_L): BINARY_OP(<<,OP_SHOVEL_L,int); DISPATCH_BOTTOM();
      CASE_OP(SHOVEL_R): BINARY_OP(>>,OP_SHOVEL_R,int); DISPATCH_BOTTOM();
      CASE_OP(NEGATE): {
          Value val = VM_PEEK(0);
          if (UNLIKELY(!IS_NUMBER(val))) {
              VM_POP();
              throwErrorFmt(lxTypeErrClass, "Can only negate numbers, type=%s", typeOfVal(val));
          }
          VM_PUSHSWAP(NUMBER_VAL(-AS_NUMBER(val)));
          DISPATCH_BOTTOM();
      }
      CASE_OP(LESS): {
          Value rhs = VM_POP(); // rhs
          Value lhs = VM_PEEK(0); // lhs
          if (UNLIKELY(!canCmpValues(lhs, rhs, instruction))) {
              VM_POP();
              throwErrorFmt(lxTypeErrClass,
                      "Can only compare numbers and objects with '<', lhs=%s, rhs=%s",
                      typeOfVal(lhs), typeOfVal(rhs));
          }
          if (cmpValues(lhs, rhs, instruction) < 0) {
              VM_PUSHSWAP(trueValue());
          } else {
              VM_PUSHSWAP(falseValue());
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(GREATER): {
        Value rhs = VM_POP();
        Value lhs = VM_PEEK(0);
        if (UNLIKELY(!canCmpValues(lhs, rhs, instruction))) {
            VM_POP();
            throwErrorFmt(lxTypeErrClass,
                "Can only compare numbers and objects with '>', lhs=%s, rhs=%s",
                typeOfVal(lhs), typeOfVal(rhs));
        }
        if (cmpValues(lhs, rhs, instruction) > 0) {
            VM_PUSHSWAP(trueValue());
        } else {
            VM_PUSHSWAP(falseValue());
        }
        DISPATCH_BOTTOM();
      }
      CASE_OP(EQUAL): {
          Value rhs = VM_POP();
          Value lhs = VM_PEEK(0);
          if (isValueOpEqual(lhs, rhs)) {
              VM_PUSHSWAP(trueValue());
          } else {
              VM_PUSHSWAP(falseValue());
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(NOT_EQUAL): {
          Value rhs = VM_POP();
          Value lhs = VM_PEEK(0);
          if (isValueOpEqual(lhs, rhs)) {
              VM_PUSHSWAP(falseValue());
          } else {
              VM_PUSHSWAP(trueValue());
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(NOT): {
          Value val = VM_PEEK(0);
          VM_PUSHSWAP(BOOL_VAL(!isTruthy(val)));
          DISPATCH_BOTTOM();
      }
      CASE_OP(GREATER_EQUAL): {
          Value rhs = VM_POP();
          Value lhs = VM_PEEK(0);
          if (UNLIKELY(!canCmpValues(lhs, rhs, instruction))) {
              VM_POP();
              throwErrorFmt(lxTypeErrClass,
                  "Can only compare numbers and objects with '>=', lhs=%s, rhs=%s",
                   typeOfVal(lhs), typeOfVal(rhs));
          }
          if (cmpValues(lhs, rhs, instruction) >= 0) {
              VM_PUSHSWAP(trueValue());
          } else {
              VM_PUSHSWAP(falseValue());
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(LESS_EQUAL): {
          Value rhs = VM_POP();
          Value lhs = VM_PEEK(0);
          if (UNLIKELY(!canCmpValues(lhs, rhs, instruction))) {
              VM_POP();
              throwErrorFmt(lxTypeErrClass,
                  "Can only compare numbers and objects with '<=', lhs=%s, rhs=%s",
                   typeOfVal(lhs), typeOfVal(rhs));
          }
          if (cmpValues(lhs, rhs, instruction) <= 0) {
              VM_PUSHSWAP(trueValue());
          } else {
              VM_PUSHSWAP(falseValue());
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(PRINT): {
          Value val = VM_POP();
          if (!vm.printBuf || vm.printToStdout) {
              printValue(stdout, val, true, -1);
              printf("\n");
#ifdef LOX_TEST
              fflush(stdout);
#endif
          }
#ifndef NDEBUG
          // used during testing
          if (vm.printBuf) {
              ObjString *out = valueToString(val, hiddenString, NEWOBJ_FLAG_NONE);
              ASSERT(out);
              pushCString(vm.printBuf, out->chars, strlen(out->chars));
              pushCString(vm.printBuf, "\n", 1);
              unhideFromGC(TO_OBJ(out));
          }
#endif
          DISPATCH_BOTTOM();
      }
      CASE_OP(DEFINE_GLOBAL): {
          Value varName = READ_CONSTANT();
          char *name = AS_CSTRING(varName);
          if (UNLIKELY(isUnredefinableGlobal(name))) {
              VM_POP();
              throwErrorFmt(lxNameErrClass, "Can't redefine global variable '%s'", name);
          }
          Value val = VM_PEEK(0);
          tableSet(&vm.globals, varName, val);
          VM_POP();
          DISPATCH_BOTTOM();
      }
      CASE_OP(GET_GLOBAL): {
          Value varName = READ_CONSTANT();
          Value val;
          if (tableGet(&EC->roGlobals, varName, &val)) {
              if (IS_STRING(val)) {
                  VM_PUSH(OBJ_VAL(dupString(AS_STRING(val))));
              } else {
                  VM_PUSH(val);
              }
          } else if (tableGet(&vm.globals, varName, &val)) {
              VM_PUSH(val);
          } else if (tableGet(&vm.constants, varName, &val)) { // for try/catch
              VM_PUSH(val);
          } else {
              throwErrorFmt(lxNameErrClass, "Undefined global variable '%s'.", AS_STRING(varName)->chars);
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(UNPACK_DEFINE_GLOBAL): {
          Value varName = READ_CONSTANT();
          uint8_t unpackIdx = READ_BYTE();
          Value val = unpackValue(peek(0), unpackIdx);
          tableSet(&vm.globals, varName, val);
          DISPATCH_BOTTOM();
      }
      CASE_OP(SET_GLOBAL): {
          Value val = VM_PEEK(0);
          Value varName = READ_CONSTANT();
          char *name = AS_CSTRING(varName);
          if (UNLIKELY(isUnredefinableGlobal(name))) {
              throwErrorFmt(lxNameErrClass, "Can't redefine global variable '%s'", name);
          }
          tableSet(&vm.globals, varName, val);
          DISPATCH_BOTTOM();
      }
      CASE_OP(NIL): {
          VM_PUSH(nilValue());
          DISPATCH_BOTTOM();
      }
      CASE_OP(TRUE): {
          VM_PUSH(BOOL_VAL(true));
          DISPATCH_BOTTOM();
      }
      CASE_OP(FALSE): {
          VM_PUSH(BOOL_VAL(false));
          DISPATCH_BOTTOM();
      }
      CASE_OP(AND): {
          Value rhs = VM_POP();
          Value lhs = VM_PEEK(0);
          (void)lhs;
          // NOTE: we only check truthiness of rhs because lhs is
          // short-circuited (a JUMP_IF_FALSE is output in the bytecode for
          // the lhs).
          VM_PUSHSWAP(isTruthy(rhs) ? rhs : BOOL_VAL(false));
          DISPATCH_BOTTOM();
      }
      CASE_OP(OR): {
          Value rhs = VM_POP();
          Value lhs = VM_PEEK(0);
          VM_PUSHSWAP(isTruthy(lhs) || isTruthy(rhs) ? rhs : lhs);
          DISPATCH_BOTTOM();
      }
      CASE_OP(POP): {
          VM_POP();
          DISPATCH_BOTTOM();
      }
      CASE_OP(POP_CREF): {
          ASSERT(th->v_crefStack.length > 0);
          ObjClass *oldKlass = vec_pop(&th->v_crefStack);
          (void)oldKlass;
          DBG_ASSERT(TO_OBJ(oldKlass) == AS_OBJ(peek(0)));
          VM_POP();
          popThis();
          DISPATCH_BOTTOM();
      }
      CASE_OP(POP_N): {
          VM_POPN((int)READ_BYTE());
          DISPATCH_BOTTOM();
      }
      CASE_OP(SET_LOCAL): {
          uint8_t slot = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          if (slot+1 > scope->localsTable.size) {
            growLocalsTable(scope, slot+1);
          }
          scope->localsTable.tbl[slot] = VM_PEEK(0); // locals are popped at end of scope by VM
          frame->slots[slot] = VM_PEEK(0);
          DISPATCH_BOTTOM();
      }
      CASE_OP(UNPACK_SET_LOCAL): {
          uint8_t slot = READ_BYTE();
          uint8_t unpackIdx = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          // make sure we don't clobber the unpack array with the setting of
          // this variable
          int peekIdx = 0;
          while (frame->slots+slot > EC->stackTop-1) {
              VM_PUSH(NIL_VAL);
              peekIdx++;
          }
          frame->slots[slot] = unpackValue(peek(peekIdx+unpackIdx), unpackIdx); // locals are popped at end of scope by VM
          DISPATCH_BOTTOM();
      }
      CASE_OP(GET_LOCAL): {
          uint8_t slot = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          if (scope->localsTable.size > slot) {
            VM_PUSH(scope->localsTable.tbl[slot]);
          } else {
            VM_PUSH(frame->slots[slot]);
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(GET_UPVALUE): {
          uint8_t slot = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          VM_PUSH(*frame->closure->upvalues[slot]->value);
          DISPATCH_BOTTOM();
      }
      CASE_OP(SET_UPVALUE): {
          uint8_t slot = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          *frame->closure->upvalues[slot]->value = peek(0);
          DISPATCH_BOTTOM();
      }
      CASE_OP(CLOSE_UPVALUE): {
          closeUpvalues(EC->stackTop - 1); // close over the top of stack value
          VM_POP(); // pop the variable off the stack frame
          DISPATCH_BOTTOM();
      }
      CASE_OP(GET_CONST): {
          Value varName = READ_CONSTANT();
          Value val;
          ObjClass *cref = NULL;
          if (th->v_crefStack.length > 0) {
              cref = TO_CLASS(vec_last(&th->v_crefStack));
          }
          if (findConstantUnder(cref, AS_STRING(varName), &val)) {
              VM_PUSH(val);
              DISPATCH_BOTTOM();
          }
          // not found, try to autoload it
          Value autoloadPath;
          if (tableGet(&vm.autoloadTbl, varName, &autoloadPath)) {
              Value requireScriptFn = NIL_VAL;
              tableGet(&vm.globals, OBJ_VAL(INTERN("requireScript")), &requireScriptFn);
              callFunctionValue(requireScriptFn, 1, &autoloadPath);
              if (findConstantUnder(cref, AS_STRING(varName), &val)) {
                  VM_PUSH(val);
                  DISPATCH_BOTTOM();
              }
          }
          if (cref) {
              throwErrorFmt(lxNameErrClass, "Undefined constant '%s::%s'.", className(cref), AS_STRING(varName)->chars);
          } else {
              throwErrorFmt(lxNameErrClass, "Undefined constant '%s'.", AS_STRING(varName)->chars);
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(GET_CONST_UNDER): {
          Value klass = VM_POP();
          Value varName = READ_CONSTANT();
          Value val;
          if (IS_NIL(klass)) {
              if (tableGet(&vm.constants, varName, &val)) {
                  VM_PUSH(val);
              } else {
                  throwErrorFmt(lxNameErrClass, "Undefined constant '%s'.", AS_STRING(varName)->chars);
              }
          } else {
              if (!IS_CLASS(klass) && !IS_MODULE(klass)) {
                  throwErrorFmt(lxTypeErrClass, "Constants must be defined under classes/modules");
              }
              if (tableGet(CLASSINFO(AS_CLASS(klass))->constants, varName, &val)) {
                  VM_PUSH(val);
              } else {
                  throwErrorFmt(lxNameErrClass, "Undefined constant '%s::%s'.", className(AS_CLASS(klass)), AS_STRING(varName)->chars);
              }
        }
          DISPATCH_BOTTOM();
      }
      CASE_OP(SET_CONST): {
          Value constName = READ_CONSTANT();
          Value val = VM_PEEK(0);
          if (th->v_crefStack.length > 0) {
              Value ownerKlass = OBJ_VAL(vec_last(&th->v_crefStack));
              addConstantUnder(AS_STRING(constName)->chars, val, ownerKlass);
          } else {
              tableSet(&vm.constants, constName, val);
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(CLOSURE): {
          Value funcVal = READ_CONSTANT();
          ASSERT(IS_FUNCTION(funcVal));
          ObjFunction *func = AS_FUNCTION(funcVal);
          ObjClosure *closure = newClosure(func, NEWOBJ_FLAG_NONE);
          VM_PUSH(OBJ_VAL(closure));

          for (int i = 0; i < closure->upvalueCount; i++) {
              uint8_t isLocal = READ_BYTE();
              uint8_t index = READ_BYTE();
              if (isLocal) {
                  // Make an new upvalue to close over the parent's local variable.
                  closure->upvalues[i] = captureUpvalue(getFrame()->slots + index);
              } else {
                  // Use the same upvalue as the current call frame.
                  closure->upvalues[i] = getFrame()->closure->upvalues[index];
              }
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(JUMP_IF_FALSE): {
          Value cond = VM_POP();
          uint8_t ipOffset = READ_BYTE();
          if (!isTruthy(cond)) {
              DBG_ASSERT(ipOffset > 0);
              frame->ip += (ipOffset-1);
          }
          VM_CHECK_INTS(vm.curThread);
          DISPATCH_BOTTOM();
      }
      CASE_OP(JUMP_IF_TRUE): {
          Value cond = VM_POP();
          uint8_t ipOffset = READ_BYTE();
          if (isTruthy(cond)) {
              DBG_ASSERT(ipOffset > 0);
              frame->ip += (ipOffset-1);
          }
          VM_CHECK_INTS(vm.curThread);
          DISPATCH_BOTTOM();
      }
      CASE_OP(JUMP_IF_FALSE_PEEK): {
          Value cond = VM_PEEK(0);
          uint8_t ipOffset = READ_BYTE();
          if (!isTruthy(cond)) {
              DBG_ASSERT(ipOffset > 0);
              frame->ip += (ipOffset-1);
          }
          VM_CHECK_INTS(vm.curThread);
          DISPATCH_BOTTOM();
      }
      CASE_OP(JUMP_IF_TRUE_PEEK): {
          Value cond = VM_PEEK(0);
          uint8_t ipOffset = READ_BYTE();
          if (isTruthy(cond)) {
              DBG_ASSERT(ipOffset > 0);
              frame->ip += (ipOffset-1);
          }
          VM_CHECK_INTS(vm.curThread);
          DISPATCH_BOTTOM();
      }
      CASE_OP(JUMP): {
          uint8_t ipOffset = READ_BYTE();
          ASSERT(ipOffset > 0);
          frame->ip += (ipOffset-1);
          VM_CHECK_INTS(vm.curThread);
          DISPATCH_BOTTOM();
      }
      CASE_OP(LOOP): {
          uint8_t ipOffset = READ_BYTE();
          ASSERT(ipOffset > 0);
          // add 1 for the instruction we just read, and 1 to go 1 before the
          // instruction we want to execute next.
          frame->ip -= (ipOffset+2);
          VM_CHECK_INTS(vm.curThread);
          DISPATCH_BOTTOM();
      }
      CASE_OP(BLOCK_BREAK): {
          Value err = newError(lxBreakBlockErrClass, NIL_VAL);
          throwError(err); // blocks catch this, not propagated
          DISPATCH_BOTTOM();
      }
      CASE_OP(BLOCK_CONTINUE): {
          Value ret;
          ObjString *key = INTERN("ret");
          if (th->lastValue) {
              ret = *th->lastValue;
          } else {
              ret = NIL_VAL;
          }
          Value err = newError(lxContinueBlockErrClass, NIL_VAL);
          setProp(err, key, ret);
          throwError(err); // blocks catch this, not propagated
          DISPATCH_BOTTOM();
      }
      CASE_OP(BLOCK_RETURN): {
          ObjString *key = INTERN("ret");
          Value ret = VM_PEEK(0);
          Value err = newError(lxReturnBlockErrClass, NIL_VAL);
          setProp(err, key, ret);
          VM_POP();
          throwError(err); // blocks catch this, not propagated
          DISPATCH_BOTTOM();
      }
      CASE_OP(TO_BLOCK): {
          Value func = VM_PEEK(0);
          if (UNLIKELY(!isCallable(func))) {
              VM_POP();
              throwErrorFmt(lxTypeErrClass, "Cannot use '&' operator on a non-function");
          }
          VM_PUSHSWAP(newBlock(AS_OBJ(func)));
          DISPATCH_BOTTOM();
      }
      CASE_OP(CALL): {
          uint8_t numArgs = READ_BYTE();
          if (th->lastSplatNumArgs >= 0) {
              numArgs += (th->lastSplatNumArgs-1);
              th->lastSplatNumArgs = -1;
          }
          Value callableVal = VM_PEEK(numArgs);
          if (UNLIKELY(!isCallable(callableVal))) {
              for (int i = 0; i < numArgs; i++) {
                  VM_POP();
              }
              throwErrorFmt(lxTypeErrClass, "Tried to call uncallable object (type=%s)", typeOfVal(callableVal));
          }
          Value callInfoVal = READ_CONSTANT();
          CallInfo *callInfo = internalGetData(AS_INTERNAL(callInfoVal));
          callCallable(callableVal, numArgs, false, callInfo);
          ASSERT_VALID_STACK();
          DISPATCH_BOTTOM();
      }
      CASE_OP(CHECK_KEYWORD): {
          Value kwMap = VM_PEEK(0);
          ASSERT(IS_T_MAP(kwMap));
          uint8_t kwSlot = READ_BYTE();
          uint8_t mapSlot = READ_BYTE();
          (void)mapSlot; // unused
          if (IS_UNDEF(getFrame()->slots[kwSlot])) {
              VM_PUSH(BOOL_VAL(false));
          } else {
              VM_PUSH(BOOL_VAL(true));
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(INVOKE): { // invoke methods (includes static methods)
          Value methodName = READ_CONSTANT();
          ObjString *mname = AS_STRING(methodName);
          uint8_t numArgs = READ_BYTE();
          Value callInfoVal = READ_CONSTANT();
          CallInfo *callInfo = internalGetData(AS_INTERNAL(callInfoVal));
          if (th->lastSplatNumArgs >= 0) {
              if (th->lastSplatNumArgs > 0) {
                  numArgs += (th->lastSplatNumArgs-1);
              }
              th->lastSplatNumArgs = -1;
          }
          Value instanceVal = VM_PEEK(numArgs);
          if (IS_CLASS(instanceVal) || IS_MODULE(instanceVal)) {
              ObjClass *klass = AS_CLASS(instanceVal);
              Obj *callable = classFindStaticMethod(klass, mname);
              if (!callable && numArgs == 0) {
                  callable = instanceFindGetter((ObjInstance*)klass, mname);
              }
              if (UNLIKELY(!callable)) {
                  ObjString *className = CLASSINFO(klass)->name;
                  const char *modStr = IS_CLASS(instanceVal) ? "class" : "module";
                  const char *classStr = className ? className->chars : "(anon)";
                  // TODO: use full name
                  throwErrorFmt(lxErrClass, "%s method '%s.%s' not found", modStr, classStr, mname->chars);
              }
              EC->stackTop[-numArgs-1] = instanceVal;
              callCallable(OBJ_VAL(callable), numArgs, true, callInfo);
          } else if (IS_INSTANCE_LIKE(instanceVal)) {
              ObjInstance *inst = AS_INSTANCE(instanceVal);
              Obj *callable = instanceFindMethod(inst, mname);
              if (!callable && numArgs == 0) {
                  callable = instanceFindGetter(inst, mname);
              }
              if (UNLIKELY(!callable)) {
                  ObjString *className = CLASSINFO(inst->klass)->name;
                  const char *classStr = className->chars ? className->chars : "(anon)";
                  throwErrorFmt(lxErrClass, "instance method '%s#%s' not found", classStr, mname->chars);
              }
              callCallable(OBJ_VAL(callable), numArgs, true, callInfo);
          } else {
              throwErrorFmt(lxTypeErrClass, "Tried to invoke method '%s' on non-instance (type=%s)", mname->chars, typeOfVal(instanceVal));
          }
          ASSERT_VALID_STACK();
          DISPATCH_BOTTOM();
      }
      CASE_OP(GET_THIS): {
          ASSERT(th->thisObj);
          VM_PUSH(OBJ_VAL(th->thisObj));
          DISPATCH_BOTTOM();
      }
      CASE_OP(SPLAT_ARRAY): {
          Value ary = VM_POP();
          if (UNLIKELY(!IS_AN_ARRAY(ary))) {
              throwErrorFmt(lxTypeErrClass, "Splatted expression must evaluate to an Array (type=%s)",
                      typeOfVal(ary));
          }
          th->lastSplatNumArgs = ARRAY_SIZE(ary); // can be 0
          for (int i = 0; i < th->lastSplatNumArgs; i++) {
              VM_PUSH(ARRAY_GET(ary, i));
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(GET_SUPER): {
          Value methodName = READ_CONSTANT();
          ASSERT(th->thisObj);
          Value instanceVal = OBJ_VAL(th->thisObj);
          ASSERT(IS_INSTANCE_LIKE(instanceVal));
          ObjClass *klass = (ObjClass*)frame->closure->function->klass; // NOTE: class or module
          ASSERT(klass);
          ObjIClass *iclassFound = NULL;
          if (TO_OBJ(klass)->type == OBJ_T_MODULE) {
              ObjModule *mod = (ObjModule*)klass;
              klass = AS_INSTANCE(instanceVal)->klass;
              Obj *iclass = TO_OBJ(klass);
              while (iclass != TO_OBJ(mod)) {
                  iclass = CLASSINFO(iclass)->superclass;
                  if (iclass->type == OBJ_T_CLASS) {
                      // do nothing
                  } else if (iclass->type == OBJ_T_ICLASS) {
                      iclassFound = (ObjIClass*)iclass;
                      iclass = TO_OBJ(((ObjIClass*)iclass)->mod);
                  }
              }
              ASSERT(iclass == TO_OBJ(mod));
              klass = (ObjClass*)iclassFound;
          }
          Value method;
          bool found = lookupMethod(
              AS_INSTANCE(instanceVal), TO_OBJ(klass),
              AS_STRING(methodName), &method, false);
          if (UNLIKELY(!found)) {
              throwErrorFmt(lxErrClass, "Could not find method '%s' for 'super': %s",
                      AS_CSTRING(methodName));
          }
          ObjBoundMethod *bmethod = newBoundMethod(AS_INSTANCE(instanceVal), AS_OBJ(method), NEWOBJ_FLAG_NONE);
          VM_PUSH(OBJ_VAL(bmethod));
          DISPATCH_BOTTOM();
      }
      // return from function/method, and close all upvalues in the callframe frame
      CASE_OP(RETURN): {
          // this is if we're in a block given by (&block), and we returned
          // (explicitly or implicitly)
          if (!getFrame()->isEval && th->v_blockStack.length > 0) {
              ObjString *key = INTERN("ret");
              VM_POP();
              Value ret;
              if (th->lastValue) {
                  ret = *th->lastValue;
              } else {
                  ret = NIL_VAL;
              }
              Value err = newError(lxContinueBlockErrClass, NIL_VAL);
              setProp(err, key, ret);
              throwError(err); // blocks catch this, not propagated
              // not reached
          }
          Value result = VM_POP(); // pop from caller's frame
          ASSERT(!getFrame()->isCCall);
          Value *newTop = getFrame()->slots;
          if (getFrame()->isEval) {
            newTop = getFrame()->slots+1;
          }
          popFrame();
          EC->stackTop = newTop;
          VM_PUSH(result);
          (th->vmRunLvl)--;
          return INTERPRET_OK;
      }
      CASE_OP(ITER): {
          Value iterable = VM_PEEK(0);
          if (UNLIKELY(!isIterableType(iterable))) {
              throwErrorFmt(lxTypeErrClass, "Non-iterable value given to 'foreach' statement. Type found: %s",
                      typeOfVal(iterable));
          }
          Value iterator = createIterator(iterable);
          DBG_ASSERT(isIterator(iterator));
          DBG_ASSERT(isIterableType(VM_PEEK(0)));
          VM_PUSHSWAP(iterator);
          DISPATCH_BOTTOM();
      }
      CASE_OP(ITER_NEXT): {
          Value iterator = VM_PEEK(0);
          ASSERT(isIterator(iterator));
          Value next = iteratorNext(iterator);
          ASSERT(!IS_UNDEF(next));
          VM_PUSH(next);
          DISPATCH_BOTTOM();
      }
      CASE_OP(CLASS): { // add or re-open class
          Value classNm = READ_CONSTANT();
          Value existingClass;
          Value ownerClass = NIL_VAL;
          Table *constantTbl = &vm.constants;
          if (th->v_crefStack.length > 0) {
              ownerClass = OBJ_VAL(vec_last(&th->v_crefStack));
              constantTbl = CLASSINFO(AS_CLASS(ownerClass))->constants;
          }

          if (tableGet(constantTbl, classNm, &existingClass)) {
              if (IS_CLASS(existingClass)) { // re-open class
                  VM_PUSH(existingClass);
                  setThis(0);
                  pushCref(AS_CLASS(existingClass));
                  DISPATCH_BOTTOM();
              } else if (UNLIKELY(IS_MODULE(existingClass))) {
                  const char *classStr = AS_CSTRING(classNm);
                  throwErrorFmt(lxTypeErrClass, "Tried to define class %s, but it's a module",
                          classStr);
              }
          }
          ObjClass *klass = newClass(AS_STRING(classNm), lxObjClass, NEWOBJ_FLAG_OLD);
          VM_PUSH(OBJ_VAL(klass));
          setThis(0);
          if (th->v_crefStack.length > 0) {
              addConstantUnder(className(klass), OBJ_VAL(klass), ownerClass);
              CLASSINFO(klass)->under = AS_OBJ(ownerClass);
          } else {
              tableSet(&vm.constants, classNm, OBJ_VAL(klass));
          }
          pushCref(klass);
          DISPATCH_BOTTOM();
      }
      CASE_OP(MODULE): { // add or re-open module
          Value modName = READ_CONSTANT();
          Value existingMod;
          Value ownerClass = NIL_VAL;
          Table *constantTbl = &vm.constants;
          if (th->v_crefStack.length > 0) {
              ownerClass = OBJ_VAL(vec_last(&th->v_crefStack));
              constantTbl = CLASSINFO(AS_CLASS(ownerClass))->constants;
          }
          if (tableGet(constantTbl, modName, &existingMod)) {
              if (IS_MODULE(existingMod)) {
                VM_PUSH(existingMod); // re-open the module
                setThis(0);
                pushCref(AS_CLASS(existingMod));
                DISPATCH_BOTTOM();
              } else if (UNLIKELY(IS_CLASS(existingMod))) {
                  const char *modStr = AS_CSTRING(modName);
                  throwErrorFmt(lxTypeErrClass, "Tried to define module %s, but it's a class",
                          modStr);
              }
          }
          ObjModule *mod = newModule(AS_STRING(modName), NEWOBJ_FLAG_OLD);
          VM_PUSH(OBJ_VAL(mod));
          setThis(0);
          if (th->v_crefStack.length > 0) {
              addConstantUnder(className(TO_CLASS(mod)), OBJ_VAL(mod), ownerClass);
              CLASSINFO(mod)->under = AS_OBJ(ownerClass);
          } else {
              tableSet(&vm.constants, modName, OBJ_VAL(mod));
          }
          pushCref(TO_CLASS(mod));
          DISPATCH_BOTTOM();
      }
      CASE_OP(SUBCLASS): { // add new class inheriting from an existing class
          Value classNm = READ_CONSTANT();
          Value superclass =  VM_POP();
          Value ownerClass = NIL_VAL;
          Table *constantTbl = &vm.constants;
          if (th->v_crefStack.length > 0) {
              ownerClass = OBJ_VAL(vec_last(&th->v_crefStack));
              constantTbl = CLASSINFO(AS_CLASS(ownerClass))->constants;
          }
          if (!IS_CLASS(superclass)) {
              throwErrorFmt(lxTypeErrClass,
                      "Class %s tried to inherit from non-class",
                      AS_CSTRING(classNm)
              );
          }
          Value existingClass;
          if (tableGet(constantTbl, classNm, &existingClass)) {
              if (UNLIKELY(IS_CLASS(existingClass))) {
                  throwErrorFmt(lxNameErrClass, "Class %s already exists (if "
                          "re-opening class, no superclass should be given)",
                          AS_CSTRING(classNm));
              } else if (UNLIKELY(IS_MODULE(existingClass))) {
                  throwErrorFmt(lxTypeErrClass, "Tried to define class %s, but it's a module", AS_CSTRING(classNm));
              }
          }
          ObjClass *klass = newClass(
              AS_STRING(classNm),
              AS_CLASS(superclass),
              NEWOBJ_FLAG_OLD
          );
          if (th->v_crefStack.length > 0) {
              addConstantUnder(AS_STRING(classNm)->chars, OBJ_VAL(klass), ownerClass);
              CLASSINFO(klass)->under = AS_OBJ(ownerClass);
          } else {
              tableSet(&vm.constants, classNm, OBJ_VAL(klass));
          }
          VM_PUSH(OBJ_VAL(klass));
          setThis(0);
          pushCref(klass);
          DISPATCH_BOTTOM();
      }
      CASE_OP(IN): {
          Value classOrInst = VM_POP();
          if (IS_CLASS(classOrInst) || IS_MODULE(classOrInst)) {
              VM_PUSH(classOrInst);
          } else {
              if (!IS_INSTANCE(classOrInst)) {
                  throwErrorFmt(lxTypeErrClass, "Expression given to 'in' statement "
                          "must evaluate to a class/module/instance (type=%s)", typeOfVal(classOrInst));
              }
              ObjClass *klass = instanceSingletonClass(AS_INSTANCE(classOrInst));
              VM_PUSH(OBJ_VAL(klass));
          }
          setThis(0);
          DISPATCH_BOTTOM();
      }
      CASE_OP(METHOD): { // method definition in class or module
          Value methodName = READ_CONSTANT();
          ObjString *methStr = AS_STRING(methodName);
          Value method = peek(0); // function
          ASSERT(IS_CLOSURE(method));
          Value classOrMod = peek(1);
          ASSERT(IS_CLASS(classOrMod) || IS_MODULE(classOrMod));
          defineMethod(classOrMod, methStr, method);
          pop(); // function
          DISPATCH_BOTTOM();
      }
      CASE_OP(CLASS_METHOD): { // method definition
          Value methodName = READ_CONSTANT();
          ObjString *methStr = AS_STRING(methodName);
          defineStaticMethod(methStr);
          DISPATCH_BOTTOM();
      }
      CASE_OP(GETTER): { // getter method definition
          Value methodName = READ_CONSTANT();
          ObjString *methStr = AS_STRING(methodName);
          defineGetter(methStr);
          DISPATCH_BOTTOM();
      }
      CASE_OP(SETTER): { // setter method definition
          Value methodName = READ_CONSTANT();
          ObjString *methStr = AS_STRING(methodName);
          defineSetter(methStr);
          DISPATCH_BOTTOM();
      }
      CASE_OP(PROP_GET): {
          Value propName = READ_CONSTANT();
          ObjString *propStr = AS_STRING(propName);
          Value instance = VM_PEEK(0);
          if (UNLIKELY(!IS_INSTANCE_LIKE(instance))) {
              VM_POP();
              throwErrorFmt(lxTypeErrClass, "Tried to access property '%s' of non-instance (type: %s)", propStr->chars, typeOfVal(instance));
          }
          Value val = propertyGet(AS_INSTANCE(instance), propStr);
          VM_PUSHSWAP(val);
          DISPATCH_BOTTOM();
      }
      CASE_OP(PROP_SET): {
          Value propName = READ_CONSTANT();
          ObjString *propStr = AS_STRING(propName);
          Value rval = VM_PEEK(0);
          Value instance = VM_PEEK(1);
          if (UNLIKELY(!IS_INSTANCE_LIKE(instance))) {
              VM_POPN(2);
              throwErrorFmt(lxTypeErrClass, "Tried to set property '%s' of non-instance", propStr->chars);
          }
          propertySet(AS_INSTANCE(instance), propStr, rval);
          VM_POP(); // leave rval on stack
          VM_PUSHSWAP(rval);
          DISPATCH_BOTTOM();
      }
      CASE_OP(INDEX_GET): {
          Value lval = VM_PEEK(1); // ex: Array/String/instance object
          if (UNLIKELY(!IS_INSTANCE_LIKE(lval))) {
              throwErrorFmt(lxTypeErrClass, "Cannot call opIndexGet ('[]') on a non-instance, found a: %s", typeOfVal(lval));
          }
          ObjInstance *instance = AS_INSTANCE(lval);
          Obj *method = instanceFindMethodOrRaise(instance, vm.opIndexGetString);
          callCallable(OBJ_VAL(method), 1, true, NULL);
          DISPATCH_BOTTOM();
      }
      CASE_OP(INDEX_SET): {
          Value lval = peek(2);
          if (UNLIKELY(!IS_INSTANCE_LIKE(lval))) {
              throwErrorFmt(lxTypeErrClass, "Cannot call opIndexSet ('[]=') on a non-instance, found a: %s", typeOfVal(lval));
          }
          ObjInstance *instance = AS_INSTANCE(lval);
          Obj *method = instanceFindMethodOrRaise(instance, vm.opIndexSetString);
          callCallable(OBJ_VAL(method), 2, true, NULL);
          DISPATCH_BOTTOM();
      }
      CASE_OP(THROW): {
          Value throwable = VM_POP();
          if (IS_STRING(throwable)) {
              Value msg = throwable;
              throwable = newError(lxErrClass, msg);
          }
          if (UNLIKELY(!isThrowable(throwable))) {
              throwErrorFmt(lxTypeErrClass, "Tried to throw unthrowable value, must be a subclass of Error. "
                  "Type found: %s", typeOfVal(throwable)
              );
          }
          throwError(throwable);
          UNREACHABLE("after throw"); // should longjmp
      }
      CASE_OP(GET_THROWN): {
          Value catchTblIdx = READ_CONSTANT();
          ASSERT(IS_NUMBER(catchTblIdx));
          double idx = AS_NUMBER(catchTblIdx);
          CatchTable *tblRow = getCatchTableRow((int)idx);
          if (UNLIKELY(!isThrowable(tblRow->lastThrownValue))) { // bug
              fprintf(stderr, "Non-throwable found (BUG): %s\n", typeOfVal(tblRow->lastThrownValue));
              ASSERT(0);
          }
          VM_PUSH(tblRow->lastThrownValue);
          DISPATCH_BOTTOM();
      }
      CASE_OP(RETHROW_IF_ERR): {
          Value catchTblIdx = READ_CONSTANT();
          ASSERT(IS_NUMBER(catchTblIdx));
          double idx = AS_NUMBER(catchTblIdx);
          CatchTable *ensureRow = getCatchTableRow((int)idx);
          if (ensureRow->isEnsureRunning && !IS_NIL(th->lastErrorThrown)) {
              throwError(th->lastErrorThrown);
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(STRING): {
          Value strLit = READ_CONSTANT();
          DBG_ASSERT(IS_STRING(strLit));
          uint8_t isStatic = READ_BYTE();
          VM_PUSH(OBJ_VAL(lxStringClass));
          ObjString *buf = AS_STRING(strLit);
          if (UNLIKELY(isStatic)) {
              STRING_SET_STATIC(buf);
              VM_PUSH(OBJ_VAL(buf));
          } else {
              VM_PUSH(OBJ_VAL(buf));
          }
          callCallable(peek(1), 1, false, NULL);
          if (UNLIKELY(isStatic == 1)) {
              objFreeze(AS_OBJ(peek(0)));
              STRING_SET_STATIC(AS_STRING(peek(0)));
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(ARRAY): {
          uint8_t numEls = READ_BYTE();
          Value aryVal = newArray();
          hideFromGC(AS_OBJ(aryVal));
          ValueArray *ary = &AS_ARRAY(aryVal)->valAry;
          for (int i = 0; i < numEls; i++) {
              Value el = VM_POP();
              writeValueArrayEnd(ary, el);
              OBJ_WRITE(aryVal, el);
          }
          VM_PUSH(aryVal);
          unhideFromGC(AS_OBJ(aryVal));
          DISPATCH_BOTTOM();
      }
      CASE_OP(DUPARRAY): {
          Value ary = READ_CONSTANT();
          AS_ARRAY(ary)->klass = lxAryClass; // NOTE: this is actually needed
          DBG_ASSERT(IS_AN_ARRAY(ary));
          VM_PUSH(arrayDup(ary));
          DISPATCH_BOTTOM();
      }
      CASE_OP(MAP): {
          uint8_t numKeyVals = READ_BYTE();
          DBG_ASSERT(numKeyVals % 2 == 0);
          Value mapVal = newMap();
          hideFromGC(AS_OBJ(mapVal));
          Table *map = AS_MAP(mapVal)->table;
          for (int i = 0; i < numKeyVals; i+=2) {
              Value key = VM_POP();
              Value val = VM_POP();
              tableSet(map, key, val);
              OBJ_WRITE(mapVal, key);
              OBJ_WRITE(mapVal, val);
          }
          VM_PUSH(mapVal);
          unhideFromGC(AS_OBJ(mapVal));
          DISPATCH_BOTTOM();
      }
      CASE_OP(DUPMAP): {
          Value map = READ_CONSTANT();
          DBG_ASSERT(IS_A_MAP(map));
          VM_PUSH(mapDup(map));
          DISPATCH_BOTTOM();
      }
      CASE_OP(REGEX): {
          Value reStr = READ_CONSTANT();
          DBG_ASSERT(IS_STRING(reStr));
          Value re;
          if (tableGet(&vm.regexLiterals, reStr, &re)) {
              VM_PUSH(re);
          } else {
              re = compileRegex(AS_STRING(reStr));
              GC_OLD(AS_OBJ(re));
              objFreeze(AS_OBJ(re));
              tableSet(&vm.regexLiterals, reStr, re);
              VM_PUSH(re);
          }
          DISPATCH_BOTTOM();
      }
      // exit interpreter, or evaluation context if in eval() or
      // loadScript/requireScript
      CASE_OP(LEAVE): {
          if (th == vm.mainThread && !isInEval() && !isInLoadedScript()) {
              vm.exited = true;
          }
          (th->vmRunLvl)--;
          return INTERPRET_OK;
      }
#ifndef COMPUTED_GOTO
      default:
          errorPrintScriptBacktrace("Unknown opcode instruction: %s (%d)", opName(instruction), instruction);
          (th->vmRunLvl)--;
          return INTERPRET_RUNTIME_ERROR;
    } // switch
#endif

  UNREACHABLE_RETURN(INTERPRET_RUNTIME_ERROR);
#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

static void setupPerScriptROGlobals(char *filename) {
    ObjString *file = copyString(filename, strlen(filename), NEWOBJ_FLAG_OLD);
    Value fileString = OBJ_VAL(file);
    hideFromGC(AS_OBJ(fileString));
    // NOTE: this can trigger GC, so we hide the value first
    tableSet(&EC->roGlobals, OBJ_VAL(vm.fileString), fileString);
    unhideFromGC(AS_OBJ(fileString));

    tableSet(&EC->roGlobals, OBJ_VAL(vm.funcString), OBJ_VAL(vm.mainString));

    if (filename[0] == pathSeparator) {
        char *lastSep = rindex(filename, pathSeparator);
        int len = lastSep - filename;
        ObjString *dir = copyString(filename, len, NEWOBJ_FLAG_OLD);
        Value dirVal = OBJ_VAL(dir);
        hideFromGC(AS_OBJ(dirVal));
        // NOTE: this can trigger GC, so we hide the value first
        tableSet(&EC->roGlobals, OBJ_VAL(vm.dirString), dirVal);
        unhideFromGC(AS_OBJ(dirVal));
    } else {
        tableSet(&EC->roGlobals, OBJ_VAL(vm.dirString), NIL_VAL);
    }
}

InterpretResult interpret(ObjFunction *func, char *filename) {
    ASSERT(func);
    Chunk *chunk = func->chunk;
    if (!EC) {
        return INTERPRET_UNINITIALIZED; // call initVM() first!
    }
    EC->filename = copyString(filename, strlen(filename), NEWOBJ_FLAG_OLD);
    EC->frameCount = 0;
    VM_DEBUG(1, "%s", "Pushing initial callframe");
    CallFrame *frame = pushFrame(func);
    frame->start = 0;
    frame->ip = chunk->code;
    frame->slots = EC->stack;
    frame->closure = newClosure(func, NEWOBJ_FLAG_OLD);
    unhideFromGC(TO_OBJ(func));
    frame->isCCall = false;
    frame->nativeFunc = NULL;
    setupPerScriptROGlobals(filename);

    InterpretResult result = vm_run0();
    return result;
}

static void *vm_run_protect(void *arg) {
    (void)arg;
    vm_run();
    return NULL;
}

// NOTE: `filename` may be on stack in caller, must be copied to use
InterpretResult loadScript(ObjFunction *func, char *filename) {
    ASSERT(func);
    Chunk *chunk = func->chunk;
    CallFrame *oldFrame = getFrame();
    push_EC(true);
    resetStack();
    VMExecContext *ectx = EC;
    EC->loadContext = true;
    EC->filename = copyString(filename, strlen(filename), NEWOBJ_FLAG_OLD);
    VM_DEBUG(1, "%s", "Pushing initial callframe");
    CallFrame *frame = pushFrame(func);
    frame->start = 0;
    frame->ip = chunk->code;
    frame->slots = EC->stack;
    frame->closure = newClosure(func, NEWOBJ_FLAG_OLD);
    unhideFromGC(TO_OBJ(func));
    frame->isCCall = false;
    frame->nativeFunc = NULL;

    setupPerScriptROGlobals(filename);

    ErrTag status = TAG_NONE;
    vm_protect(vm_run_protect, NULL, NULL, &status);
    // `EC != ectx` if an error occured in the script, and propagated out
    // due to being caught in a calling script or never being caught.
    if (EC == ectx) pop_EC();
    ASSERT(oldFrame == getFrame());
    if (status == TAG_RAISE) {
        rethrowErrInfo(THREAD()->errInfo);
        UNREACHABLE_RETURN(INTERPRET_RUNTIME_ERROR);
    } else {
        return INTERPRET_OK;
    }
}

static Value doVMEval(char *src, char *filename, int lineno, bool throwOnErr) {
    CallFrame *oldFrame = getFrame(); // eval() native frame, if called from there
    CallFrame *prevFrame = oldFrame;
    bool oldFrameIsEval = oldFrame->isCCall;
    if (oldFrameIsEval) {
      VM_DEBUG(1, "oldFrame is c call: %s", callFrameName(prevFrame));
      prevFrame = prevFrame->prev;
    }
    CompileErr err = COMPILE_ERR_NONE;
    int oldOpts = compilerOpts.noRemoveUnusedExpressions;
    compilerOpts.noRemoveUnusedExpressions = true;
    VMExecContext *old_ectx = EC;
    push_EC(false);
    VMExecContext *ectx = EC;
    ectx->evalContext = true;
    ectx->stack = oldFrame->slots;
    ectx->stackTop = old_ectx->stackTop;
    VM_DEBUG(1, "VM eval called in func '%s', ip: %d", callFrameName(prevFrame),
        prevFrame->ip - prevFrame->closure->function->chunk->code);
    LocalVariable *var; int varidx = 0;
    vec_foreach(&prevFrame->closure->function->variables, var, varidx) {
        VM_DEBUG(1, "var from fn: %s", var->name->chars);
    }
    ObjFunction *func = compile_eval_src(src, &err, prevFrame->closure->function, prevFrame->ip);
    compilerOpts.noRemoveUnusedExpressions = oldOpts;

    if (err != COMPILE_ERR_NONE || !func) {
        if (func) {
            freeChunk(func->chunk);
            FREE(Chunk, func->chunk);
        }
        VM_DEBUG(1, "compile error in eval");
        pop_EC();
        ASSERT(getFrame() == oldFrame);
        if (throwOnErr) {
            throwErrorFmt(lxSyntaxErrClass, "%s", "Syntax error");
        } else {
            // TODO: output error messages
            return UNDEF_VAL;
        }
    }
    ectx->filename = copyString(filename, strlen(filename), NEWOBJ_FLAG_OLD);
    VM_DEBUG(1, "%s", "Pushing initial eval callframe");
    // TODO: this pushFrame() allocates a new scope, but then just re-uses the parent scope.
    // We could just skip allocation of scope for this frame.
    CallFrame *frame = pushFrame(func);
    frame->scope = prevFrame->scope;
    frame->start = 0;
    frame->ip = func->chunk->code;
    frame->slots = ectx->stack; // old frame's slots
    frame->closure = newClosure(func, NEWOBJ_FLAG_OLD);
    unhideFromGC(TO_OBJ(func));
    frame->isCCall = false;
    frame->isEval = true;
    frame->nativeFunc = NULL;
    frame->instance = NULL;
    frame->klass = NULL;

    setupPerScriptROGlobals(filename);

    ErrTag status = TAG_NONE;
    InterpretResult result = INTERPRET_OK;
    vm_protect(vm_run_protect, NULL, NULL, &status);
    if (status == TAG_RAISE) {
        result = INTERPRET_RUNTIME_ERROR;
        THREAD()->hadError = true;
    }
    Value val = *THREAD()->lastValue;
    VM_DEBUG(1, "eval finished: error: %d, result: %s", THREAD()->hadError ? 1 : 0,
        result == INTERPRET_OK ? "OK" : "RAISE");
    // `EC != ectx` if an error occured in the eval, and propagated out
    // due to being caught in a surrounding context or never being caught.
    if (EC == ectx) pop_EC();
    ASSERT(getFrame() == oldFrame);
    if (result == INTERPRET_OK) {
        return val;
    } else {
        if (throwOnErr) {
            rethrowErrInfo(THREAD()->errInfo);
            UNREACHABLE_RETURN(UNDEF_VAL);
        } else {
            // TODO: output error messages
            return UNDEF_VAL;
        }
    }
}

static Value doVMBindingEval(ObjScope *scope, char *src, char *filename, int lineno, bool throwOnErr) {
    CallFrame *oldFrame = getFrame();
    CallFrame *prevFrame = oldFrame;
    CompileErr err = COMPILE_ERR_NONE;
    int oldOpts = compilerOpts.noRemoveUnusedExpressions;
    compilerOpts.noRemoveUnusedExpressions = true;
    VMExecContext *old_ectx = EC;
    push_EC(true);
    resetStack();
    VMExecContext *ectx = EC;
    ectx->evalContext = true;
    VM_DEBUG(1, "Calling binding VM eval");
    ObjFunction *func = compile_binding_eval_src(src, &err, scope);
    compilerOpts.noRemoveUnusedExpressions = oldOpts;

    if (err != COMPILE_ERR_NONE || !func) {
        if (func) {
            freeChunk(func->chunk);
            FREE(Chunk, func->chunk);
        }
        VM_DEBUG(1, "compile error in eval");
        pop_EC();
        ASSERT(getFrame() == oldFrame);
        if (throwOnErr) {
            throwErrorFmt(lxSyntaxErrClass, "%s", "Syntax error");
        } else {
            // TODO: output error messages
            return UNDEF_VAL;
        }
    }
    ectx->filename = copyString(filename, strlen(filename), NEWOBJ_FLAG_OLD);
    VM_DEBUG(1, "%s", "Pushing initial binding eval callframe");
    // TODO: this pushFrame() allocates a new scope, but then just re-uses the parent scope.
    // We could just skip allocation of scope for this frame.
    CallFrame *frame = pushFrame(func); // FIXME: wasted a OBJ_T_SCOPE allocation
    frame->scope = scope;
    frame->start = 0;
    frame->ip = func->chunk->code;
    frame->slots = EC->stack;
    ASSERT(frame->slots);
    frame->closure = newClosure(func, NEWOBJ_FLAG_OLD);
    unhideFromGC(TO_OBJ(func));
    frame->isCCall = false;
    frame->isEval = true;
    frame->nativeFunc = NULL;
    frame->instance = NULL; // should be scope->instance
    frame->klass = NULL; // should be scope->klass

    setupPerScriptROGlobals(filename);

    ErrTag status = TAG_NONE;
    InterpretResult result = INTERPRET_OK;
    vm_protect(vm_run_protect, NULL, NULL, &status);
    if (status == TAG_RAISE) {
        result = INTERPRET_RUNTIME_ERROR;
        THREAD()->hadError = true;
    }
    Value val = *THREAD()->lastValue;
    VM_DEBUG(1, "eval finished: error: %d, result: %s", THREAD()->hadError ? 1 : 0,
        result == INTERPRET_OK ? "OK" : "RAISE");
    // `EC != ectx` if an error occured in the eval, and propagated out
    // due to being caught in a surrounding context or never being caught.
    if (EC == ectx) pop_EC();
    ASSERT(getFrame() == oldFrame);
    if (result == INTERPRET_OK) {
        return val;
    } else {
        if (throwOnErr) {
            rethrowErrInfo(THREAD()->errInfo);
            UNREACHABLE_RETURN(UNDEF_VAL);
        } else {
            // TODO: output error messages
            return UNDEF_VAL;
        }
    }
}

Value VMEvalNoThrow(char *src, char *filename, int lineno) {
    return doVMEval(src, filename, lineno, false);
}

Value VMEval(char *src, char *filename, int lineno) {
    return doVMEval(src, filename, lineno, true);
}

Value VMBindingEval(ObjScope *scope, char *src, char *filename, int lineno) {
    return doVMBindingEval(scope, src, filename, lineno, true);
}

void setPrintBuf(ObjString *buf, bool alsoStdout) {
    DBG_ASSERT(vm.inited);
    vm.printBuf = buf;
    vm.printToStdout = alsoStdout;
}

void unsetPrintBuf(void) {
    DBG_ASSERT(vm.inited);
    vm.printBuf = NULL;
    vm.printToStdout = true;
}

void unwindJumpRecover(ErrTagInfo *info) {
    DBG_ASSERT(info);
    LxThread *th = THREAD();
    CallFrame *f = getFrame();
    DBG_ASSERT(f);
    while (f != info->frame) {
        VM_DEBUG(2, "popping callframe from unwind");
        f = f->prev;
        popFrame();
    }
    while (th->errInfo != info) {
        VM_DEBUG(2, "freeing Errinfo");
        DBG_ASSERT(th->errInfo);
        ErrTagInfo *prev = th->errInfo->prev;
        DBG_ASSERT(prev);
        FREE(ErrTagInfo, th->errInfo);
        th->errInfo = prev;
    }
}

BlockStackEntry *addBlockEntry(Obj *callable) {
    /*fprintf(stderr, "Pushing block entry\n");*/
    DBG_ASSERT(callable);
    BlockStackEntry *entry = ALLOCATE(BlockStackEntry, 1);
    entry->callable = callable; // closure or native
    entry->cachedBlockClosure = NULL;
    entry->blockInstance = NULL;
    entry->frame = NULL;
    vec_push(&THREAD()->v_blockStack, entry);
    return entry;
}

void popBlockEntryUntil(BlockStackEntry *bentry) {
    DBG_ASSERT(bentry);
    LxThread *th = vm.curThread;
    BlockStackEntry *last = NULL;
    if (th->v_blockStack.length == 0) {
        /*fprintf(stderr, "Empty block stack (pop until)\n");*/
    }
    while ((last = vec_last_or(&th->v_blockStack, NULL)) != NULL && last != bentry) {
        /*fprintf(stderr, "Popping block entry (until)\n");*/
        FREE(BlockStackEntry, last);
        (void)vec_pop(&th->v_blockStack);
    }
    if (last == bentry) {
        /*fprintf(stderr, "Popping block entry (until last)\n");*/
        FREE(BlockStackEntry, last);
        (void)vec_pop(&th->v_blockStack);
    }
}

void popBlockEntry(BlockStackEntry *bentry) {
    DBG_ASSERT(bentry);
    LxThread *th = vm.curThread;
    BlockStackEntry *last = NULL;
    if ((last = vec_last_or(&th->v_blockStack, NULL)) != NULL && last == bentry) {
        /*fprintf(stderr, "Popping block entry (pop)\n");*/
        FREE(BlockStackEntry, last);
        (void)vec_pop(&th->v_blockStack);
    }
}

void *vm_protect(vm_cb_func func, void *arg, ObjClass *errClass, ErrTag *status) {
    LxThread *th = THREAD();
    volatile CallFrame *frame = getFrame();
    addErrInfo(errClass);
    volatile ErrTagInfo *errInfo = th->errInfo; // recently added
    int jmpres = 0;
    if ((jmpres = setjmp(((ErrTagInfo*)errInfo)->jmpBuf)) == JUMP_SET) {
        *status = TAG_NONE;
        VM_DEBUG(1, "vm_protect before func");
        ErrTagInfo *prev = errInfo->prev;
        void *res = func(arg);
        if (getFrameOrNull() == frame) { // frame was not popped, so we unwind the errinfo
          FREE(ErrTagInfo, (ErrTagInfo*)errInfo); // was not freed by popFrame()
          th->errInfo = prev;
        }
        VM_DEBUG(1, "vm_protect after func");
        return res;
    } else if (jmpres == JUMP_PERFORMED) {
        VM_DEBUG(1, "vm_protect got to longjmp");
        th = THREAD();
        ASSERT(errInfo == th->errInfo);
        unwindJumpRecover((ErrTagInfo*)errInfo);
        errInfo->status = TAG_RAISE;
        errInfo->caughtError = th->lastErrorThrown;
        *status = TAG_RAISE;
    } else {
        fprintf(stderr, "vm_protect: error from setjmp");
        UNREACHABLE("setjmp error");
    }
    return NULL;
}

void runAtExitHooks(void) {
    vm.exited = false;
    ObjClosure *func = NULL;
    int i = 0;
    vec_foreach_rev(&vm.exitHandlers, func, i) {
        push(OBJ_VAL(func));
        callCallable(OBJ_VAL(func), 0, false, NULL);
        pop();
    }
}

void terminateThreads() {
    int tries = 0;
    (void)tries;
    while (true) {
        int found = 0;
        int numReadyFound = 0;
        volatile ObjInstance *threadInst = NULL; int tidx = 0;
        vec_foreach(&vm.threads, threadInst, tidx) {
            LxThread *th = (LxThread*)threadInst->internal->data;
            if (th == vm.mainThread || th->status == THREAD_ZOMBIE) continue;
            if (th->tid <= 0) continue;
            if (th->status == THREAD_READY) {
                numReadyFound++;
            }
            THREAD_DEBUG(2, "Unjoined thread found (idx=%d): %lu: %s", tidx, th->tid, threadStatusName(th->status));
            found++;
            if (vm.numLivingThreads <= 1 && numReadyFound == 0) break;
            forceUnlockMutexes(th);
            threadInterrupt(th, false); // 'exit' interrupt
        }
        if (found == 0 || (vm.numLivingThreads <= 1 && numReadyFound == 0)) {
            break;
        }
        tries++;
    }
    vm.numLivingThreads = 1;
}

// TODO: rename to exitThread(), as this either stops the VM or exits the
// current non-main thread
NORETURN void stopVM(int status) {
    LxThread *th = vm.curThread;
    if (th == vm.mainThread) {
        vm.exiting = true;
        th->exitStatus = status;
        THREAD_DEBUG(1, "Main thread exiting with %d (PID=%d)", status, getpid());
        THREAD_DEBUG(1, "Terminating unjoined threads");
        terminateThreads();
        THREAD_DEBUG(1, "Running atexit hooks");
        runAtExitHooks();
        th->status = THREAD_ZOMBIE;
        freeVM();
        if (GET_OPTION(profileGC)) {
            printGCProfile();
        }
        vm.exited = true;
        vm.numLivingThreads--;
        // NOTE: pthread_exit in last thread always exits with 0, so have to call _exit manually
        _exit(th->exitStatus);
    } else {
        exitingThread(th);
        th->exitStatus = status;
        if (th->detached && vm.numDetachedThreads > 0) {
            THREAD_DEBUG(1, "Thread %lu [DETACHED] exiting with %d (PID=%d)", th->tid, status, getpid());
            vm.numDetachedThreads--;
        } else {
            THREAD_DEBUG(1, "Thread %lu exiting with %d (PID=%d)", th->tid, status, getpid());
        }
        th->status = THREAD_ZOMBIE;
        vm.numLivingThreads--;
        if (th->mutexCounter > 0) {
            forceUnlockMutexes(th);
        }
        th->mutexCounter = 0;
        releaseGVL(THREAD_ZOMBIE);
        pthread_exit(&status);
    }
}

NORETURN void _stopVM(int status) {
    if (vm.curThread != vm.mainThread) {
        stopVM(status);
    } else {
        LxThread *th = vm.curThread;
        vm.exiting = true;
        THREAD_DEBUG(1, "Main thread exiting with %d (PID=%d)", status, getpid());
        THREAD_DEBUG(1, "Terminating unjoined threads");
        terminateThreads();
        th->status = THREAD_ZOMBIE;
        freeVM();
        if (GET_OPTION(profileGC)) {
            printGCProfile();
        }
        vm.exited = true;
        vm.numLivingThreads--;
        _exit(status);
    }
}

void acquireGVL(void) {
    pthread_mutex_lock(&vm.GVLock);
    LxThread *th = vm.curThread;
    // this is because a releaseGVL() call may not have actually released the
    // GVL due to held mutex, so the next acquireGVL() has to take that into account
    if (th && th->mutexCounter > 0 && GVLOwner == th->tid && th->tid == pthread_self()) {
        THREAD_DEBUG(1, "Thread %lu skipping acquire of GVL due to held mutex\n", pthread_self());
        pthread_mutex_unlock(&vm.GVLock);
        return;
    }
    // This happens when an interrupt occurs while the GVL was released
    // (like during a syscall being blocked), and the interrupt handler function needs
    // to run if there's a lox Signal.trap() for that interrupt, so the GVL is
    // acquired there, and then tries to be re-acquired afterwards in the C
    // code when the syscall returns.
    if (th && GVLOwner == th->tid && th->tid == pthread_self()) {
        THREAD_DEBUG(1, "Thread %lu skipping acquire of GVL due to already being held\n", pthread_self());
        pthread_mutex_unlock(&vm.GVLock);
        return;
    }
    vm.GVLWaiters++;
    while (vm.GVLockStatus > 0) {
        pthread_cond_wait(&vm.GVLCond, &vm.GVLock); // block on wait queue
    }
    vm.GVLWaiters--;
    vm.GVLockStatus = 1;
    vm.curThread = FIND_THREAD(pthread_self());
    if (vm.curThread) {
        vm.curThread->opsRemaining = THREAD_OPS_UNTIL_SWITCH;
        if (vm.curThread->status == THREAD_ZOMBIE) {
            ASSERT(0);
        }
    }
    GVLOwner = pthread_self();
    pthread_mutex_unlock(&vm.GVLock);
    if (vm.curThread && !(IS_NIL(vm.curThread->errorToThrow))) {
        Value err = vm.curThread->errorToThrow;
        vm.curThread->errorToThrow = NIL_VAL;
        throwError(err);
    }
    VM_CHECK_INTS(vm.curThread);
}

void releaseGVL(ThreadStatus thStatus) {
    pthread_mutex_lock(&vm.GVLock);
    LxThread *th = vm.curThread;
    if (!th) { // NOTE: happens only in signal handler trap calls
        pthread_mutex_unlock(&vm.GVLock);
        return;
    }
    if (GVLOwner != th->tid) {
        THREAD_DEBUG(1, "Thread %lu skipping release of GVL due to not holding!\n", pthread_self());
        pthread_mutex_unlock(&vm.GVLock);
        return;
    }
    if (th && th->mutexCounter > 0 && th->tid == GVLOwner && th->tid == pthread_self()) {
        THREAD_DEBUG(1, "Thread %lu skipping release of GVL due to held mutex\n", pthread_self());
        pthread_mutex_unlock(&vm.GVLock);
        return;
    }
    vm.GVLockStatus = 0;
    GVLOwner = 0;
    vm.curThread = NULL;
    pthread_mutex_unlock(&vm.GVLock);
    pthread_cond_signal(&vm.GVLCond); // signal waiters
}

void threadSetCurrent(LxThread *th) {
    vm.curThread = th;
    GVLOwner = th->tid;
}

LxThread *FIND_THREAD(pthread_t tid) {
    ObjInstance *threadInstance; int thIdx = 0;
    LxThread *th = NULL;
    vec_foreach_rev(&vm.threads, threadInstance, thIdx) {
        th = (LxThread*)threadInstance->internal->data;
        ASSERT(th);
        if (th->tid == tid) return th;
    }
    return NULL;
}

ObjInstance *FIND_THREAD_INSTANCE(pthread_t tid) {
    ObjInstance *threadInstance; int thIdx = 0;
    LxThread *th = NULL;
    vec_foreach_rev(&vm.threads, threadInstance, thIdx) {
        th = (LxThread*)threadInstance->internal->data;
        ASSERT(th);
        if (th->tid == tid) return threadInstance;
    }
    return NULL;
}

LxThread *THREAD() {
    if (!vm.curThread) {
        pthread_mutex_lock(&vm.GVLock);
        vm.curThread = FIND_THREAD(pthread_self());
        DBG_ASSERT(vm.curThread);
        DBG_ASSERT(vm.curThread->tid == GVLOwner);
        DBG_ASSERT(vm.curThread->status != THREAD_ZOMBIE);
        vm.curThread->opsRemaining = THREAD_OPS_UNTIL_SWITCH;
        pthread_mutex_unlock(&vm.GVLock);
    }
    return vm.curThread;
}
