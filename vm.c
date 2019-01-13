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

volatile long long GVLOwner;

#ifndef NDEBUG
#define VM_DEBUG(lvl, ...) vm_debug(lvl, __VA_ARGS__)
#define VM_WARN(...) vm_warn(__VA_ARGS__)
#else
#define VM_DEBUG(...) (void)0
#define VM_WARN(...) (void)0
#endif

static void vm_debug(int lvl, const char *format, ...) {
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
static void vm_warn(const char *format, ...) {
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
    diePrintCBacktrace("info:");
}

void initSighandlers(void) {
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
    "debugger",
    "loadScript",
    "requireScript",
    "eval",
    "yield",
    "sleep",
    "__FILE__",
    "__DIR__",
    "__LINE__",
    NULL
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
    addGlobalFunction("loadScript", lxLoadScript);
    addGlobalFunction("requireScript", lxRequireScript);
    addGlobalFunction("debugger", lxDebugger);
    addGlobalFunction("eval", lxEval);
    addGlobalFunction("sleep", lxSleep);
    addGlobalFunction("yield", lxYield);
    addGlobalFunction("exit", lxExit);
    addGlobalFunction("atExit", lxAtExit);
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
ObjClass *lxLoadErrClass;

// internal error classes for flow control of blocks
ObjClass *lxBlockIterErrClass;
ObjClass *lxBreakBlockErrClass;
ObjClass *lxContinueBlockErrClass;
ObjClass *lxReturnBlockErrClass;

ObjArray *lxLoadPath; // load path for loadScript/requireScript (-L flag)
ObjArray *lxArgv;

ObjNative *nativeObjectInit = NULL;
ObjNative *nativeIteratorInit = NULL;
ObjNative *nativeErrorInit = NULL;
ObjNative *nativeClassInit = NULL;
ObjNative *nativeModuleInit = NULL;

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
    addNativeMethod(objClass, "freeze", lxObjectFreeze);
    addNativeMethod(objClass, "unfreeze", lxObjectUnfreeze);
    addNativeMethod(objClass, "isFrozen", lxObjectIsFrozen);
    addNativeMethod(objClass, "send", lxObjectSend);
    addNativeGetter(objClass, "class", lxObjectGetClass);
    addNativeGetter(objClass, "singletonClass", lxObjectGetSingletonClass);
    addNativeGetter(objClass, "objectId", lxObjectGetObjectId);

    // class Module
    ObjClass *modClass = addGlobalClass("Module", objClass);
    lxModuleClass = modClass;

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
    addNativeMethod(classClass, "include", lxClassInclude);
    addNativeGetter(classClass, "superClass", lxClassGetSuperclass);
    addNativeGetter(classClass, "name", lxClassGetName);

    nativeModuleInit = addNativeMethod(modClass, "init", lxModuleInit);

    Init_ArrayClass();

    Init_MapClass();

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
    Init_ProcessModule();
    Init_IOClass();
    Init_FileClass();
    Init_ThreadClass();
    Init_TimeClass();
    Init_BlockClass();
    isClassHierarchyCreated = true;
}

// NOTE: this initialization function can create Lox runtime objects
static void defineGlobalVariables(void) {
    Value loadPathVal = newArray();
    ASSERT(IS_ARRAY(loadPathVal));
    lxLoadPath = AS_ARRAY(loadPathVal);
    ObjString *loadPathStr = INTERN("loadPath");
    ASSERT(tableSet(&vm.globals, OBJ_VAL(loadPathStr), loadPathVal));
    // add 'lib' path as default load path
    ObjString *libPath = hiddenString("", 0, NEWOBJ_FLAG_OLD);
    // FIXME: this won't work if user moves their clox binary after building
    pushCString(libPath, QUOTE(LX_BUILT_DIR), strlen(QUOTE(LX_BUILT_DIR)));
    pushCString(libPath, "/lib", strlen("/lib"));
    arrayPush(loadPathVal, OBJ_VAL(libPath));
    unhideFromGC(TO_OBJ(libPath));
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
        Obj *method = instanceFindMethodOrRaise(instance, iterId);
        callVMMethod(instance, OBJ_VAL(method), 0, NULL, NULL);
        Value ret = pop();
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

// Add and use a new execution context
static inline void push_EC(void) {
    LxThread *th = THREAD();
    VMExecContext *ectx = ALLOCATE(VMExecContext, 1);
    memset(ectx, 0, sizeof(*ectx));
    initTable(&ectx->roGlobals);
    vec_push(&th->v_ecs, ectx);
    th->ec = ectx; // EC = ectx
}

// Pop the current execution context and use the one created before
// the current one.
static inline void pop_EC(void) {
    LxThread *th = THREAD();
    ASSERT(th->v_ecs.length > 0);
    VMExecContext *ctx = (VMExecContext*)vec_pop(&th->v_ecs);
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

    Value mainThread = newThread();
    LxThread *th = THREAD_GETHIDDEN(mainThread);

    vm.curThread = th;
    vm.mainThread = th;
    vec_init(&vm.threads);
    vec_push(&vm.threads, AS_INSTANCE(mainThread));

    pthread_t tid = pthread_self();
    threadSetId(mainThread, tid);
    acquireGVL();
    threadSetStatus(mainThread, THREAD_RUNNING);
    THREAD_DEBUG(1, "Main thread initialized");
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
    vm.lastOp = -1;

    initTable(&vm.globals);
    initTable(&vm.strings); // interned strings
    vec_init(&vm.hiddenObjs);

    ObjInstance *mainT = initMainThread();
    push_EC();
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
    vm.initString = INTERNED("init", 4);
    vm.fileString = INTERNED("__FILE__", 8);
    vm.dirString = INTERNED("__DIR__", 7);

    pushFrame();


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

    freeObjects();

    freeDebugger(&vm.debugger);
    vm.instructionStepperOn = false;

    curLine = 1;

    memset(&rootVMLoopJumpBuf, 0, sizeof(rootVMLoopJumpBuf));
    rootVMLoopJumpBufSet = false;

    freeTable(&vm.globals);
    freeTable(&vm.strings);
    vm.initString = NULL;
    vm.fileString = NULL;
    vm.dirString = NULL;
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
    releaseGVL();
    if (vm.numDetachedThreads <= 0) {
        pthread_mutex_destroy(&vm.GVLock);
        pthread_cond_destroy(&vm.GVLCond);
    }
    vm.GVLWaiters = 0;

    vm.curThread = NULL;
    vm.mainThread = NULL;
    vec_deinit(&vm.threads);
    vm.lastOp = -1;

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

#define ASSERT_VALID_STACK() DBG_ASSERT(LIKELY(EC->stackTop >= EC->stack))

static inline bool isOpStackEmpty(void) {
    ASSERT_VALID_STACK();
    return EC->stackTop == EC->stack;
}

void push(Value value) {
    ASSERT_VALID_STACK();
    register VMExecContext *ctx = EC;
    if (UNLIKELY(ctx->stackTop >= ctx->stack + STACK_MAX)) {
        errorPrintScriptBacktrace("Stack overflow.");
        int status = 1;
        pthread_exit(&status);
    }
    if (IS_OBJ(value)) {
        DBG_ASSERT(LIKELY(AS_OBJ(value)->type != OBJ_T_NONE));
        OBJ_SET_PUSHED_VM_STACK(AS_OBJ(value)); // for gen gc
    }
    *ctx->stackTop = value;
    ctx->stackTop++;
}

static inline void pushSwap(Value value) {
    *(EC->stackTop-1) = value;
}

Value pop(void) {
    VMExecContext *ctx = EC;
    ASSERT(LIKELY(ctx->stackTop > ctx->stack));
    ctx->stackTop--;
    ctx->lastValue = ctx->stackTop;
    vm.curThread->lastValue = ctx->lastValue;
    return *(vm.curThread->lastValue);
}

Value peek(unsigned n) {
    VMExecContext *ctx = EC;
    ASSERT(LIKELY((ctx->stackTop-n) > ctx->stack));
    return *(ctx->stackTop-1-n);
}

static inline void setThis(unsigned n) {
    register VMExecContext *ctx = EC;
    if (UNLIKELY((ctx->stackTop-n) <= ctx->stack)) {
        ASSERT(0);
    }
    vm.curThread->thisObj = AS_OBJ(*(ctx->stackTop-1-n));
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
    return (IS_NUMBER(lhs) && IS_NUMBER(rhs)) ||
        (IS_STRING(lhs) && IS_STRING(rhs));
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
            ObjString *opEquals = INTERNED("opEquals", 8);
            ObjInstance *self = AS_INSTANCE(lhs);
            Obj *methodOpEq = instanceFindMethod(self, opEquals);
            if (methodOpEq) {
                Value ret = callVMMethod(self, OBJ_VAL(methodOpEq), 1, &rhs, NULL);
                pop();
                return isTruthy(ret);
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

void showUncaughtError(Value err) {
    ObjString *classNameObj = CLASSINFO(AS_INSTANCE(err)->klass)->name;
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
    // waste of time to set backtrace on an error which is just part of
    // internal control flow (break/continue/return in blocks)
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
                    char *fnName = function->name ? function->name->chars : "(anon)";
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

static bool lookupMethod(ObjInstance *obj, Obj *klass, ObjString *propName, Value *ret, bool lookInGivenClass) {
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

static Value propertyGet(ObjInstance *obj, ObjString *propName) {
    Value ret;
    Obj *method = NULL;
    Obj *getter = NULL;
    if (tableGet(obj->fields, OBJ_VAL(propName), &ret)) {
        VM_DEBUG(3, "field found (propertyGet)");
        return ret;
    } else if ((getter = instanceFindGetter(obj, propName))) {
        VM_DEBUG(3, "getter found (propertyGet)");
        callVMMethod(obj, OBJ_VAL(getter), 0, NULL, NULL);
        if (THREAD()->hadError) {
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

static void propertySet(ObjInstance *obj, ObjString *propName, Value rval) {
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

static void defineMethod(ObjString *name) {
    Value method = peek(0); // function
    ASSERT(IS_CLOSURE(method));
    Value classOrMod = peek(1);
    ASSERT(IS_CLASS(classOrMod) || IS_MODULE(classOrMod));
    ObjFunction *func = AS_CLOSURE(method)->function;
    func->klass = AS_OBJ(classOrMod);
    if (IS_CLASS(classOrMod)) {
        ObjClass *klass = AS_CLASS(classOrMod);
        const char *klassName = CLASSINFO(klass)->name ? CLASSINFO(klass)->name->chars : "(anon)";
        (void)klassName;
        VM_DEBUG(2, "defining method '%s' in class '%s'", name->chars, klassName);
        tableSet(CLASSINFO(klass)->methods, OBJ_VAL(name), method);
        OBJ_WRITE(OBJ_VAL(klass), method);
        GC_OLD(AS_OBJ(method));
    } else {
        ObjModule *mod = AS_MODULE(classOrMod);
        const char *modName = CLASSINFO(mod)->name ? CLASSINFO(mod)->name->chars : "(anon)";
        (void)modName;
        VM_DEBUG(2, "defining method '%s' in module '%s'", name->chars, modName);
        tableSet(CLASSINFO(mod)->methods, OBJ_VAL(name), method);
        OBJ_WRITE(OBJ_VAL(mod), method);
        GC_OLD(AS_OBJ(method));
    }
    pop(); // function
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
    Obj *oldThis = vm.curThread->thisObj;
    setThis(0);
    for (int i = 0; i < argCount; i++) {
        DBG_ASSERT(args); // can be null when if i = 0
        push(args[i]);
    }
    VM_DEBUG(3, "call begin");
    callCallable(callable, argCount, true, cinfo); // pushes return value to stack
    vm.curThread->thisObj = oldThis;
    VM_DEBUG(3, "call end");
    return peek(0);
}

Value callMethod(Obj *obj, ObjString *methodName, int argCount, Value *args, CallInfo *cinfo) {
    if (obj->type == OBJ_T_INSTANCE || obj->type == OBJ_T_ARRAY || obj->type == OBJ_T_STRING || obj->type == OBJ_T_MAP) {
        ObjInstance *instance = (ObjInstance*)obj;
        Obj *callable = instanceFindMethod(instance, methodName);
        if (!callable && argCount == 0) {
            callable = instanceFindGetter(instance, methodName);
        }
        if (UNLIKELY(!callable)) {
            ObjString *className = CLASSINFO(instance->klass)->name;
            const char *classStr = className->chars ? className->chars : "(anon)";
            throwErrorFmt(lxErrClass, "instance method '%s#%s' not found", classStr, methodName->chars);
        }
        callVMMethod(instance, OBJ_VAL(callable), argCount, args, cinfo);
        return pop();
    } else if (obj->type == OBJ_T_CLASS) {
        ObjClass *klass = (ObjClass*)obj;
        Obj *callable = classFindStaticMethod(klass, methodName);
        /*if (!callable && numArgs == 0) {*/
        /*callable = instanceFindGetter((ObjInstance*)klass, mname);*/
        /*}*/
        if (UNLIKELY(!callable)) {
            ObjString *className = CLASSINFO(klass)->name;
            const char *classStr = className ? className->chars : "(anon)";
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
            throwErrorFmt(lxErrClass, "module method '%s.%s' not found", modStr, methodName->chars);
        }
        callVMMethod((ObjInstance*)mod, OBJ_VAL(callable), argCount, args, cinfo);
        return pop();
    } else {
        throwErrorFmt(lxTypeErrClass, "Tried to invoke method on non-instance (type=%s)", typeOfObj(obj));
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


void popFrame(void) {
    DBG_ASSERT(vm.inited);
    register LxThread *th = vm.curThread;
    ASSERT(EC->frameCount >= 1);
    CallFrame *frame = getFrame();
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
    }
    EC->frameCount--;
    frame = getFrameOrNull(); // new frame
    if (frame) {
        if (stackAdjust > 0) {
            if (EC->stackTop-stackAdjust > EC->stack) {
                EC->stackTop -= stackAdjust;
            }
        }
        if (frame->instance) {
            th->thisObj = TO_OBJ(frame->instance);
        } else {
            th->thisObj = NULL;
        }
    }
    ASSERT_VALID_STACK();
}

CallFrame *pushFrame(void) {
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
    frame->file = EC->filename;
    frame->prev = prev;
    BlockStackEntry *bentry = vec_last_or(&THREAD()->v_blockStack, NULL);
    if (bentry && bentry->frame == NULL) {
        bentry->frame = frame;
    }
    return frame;
}

const char *callFrameName(CallFrame *frame) {
    DBG_ASSERT(frame);
    ObjString *fnName = frame->closure->function->name;
    return fnName ? fnName->chars : "<main>";
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
    CallFrame *newFrame = pushFrame();
    newFrame->closure = prevFrame->closure;
    newFrame->ip = prevFrame->ip;
    newFrame->start = 0;
    newFrame->slots = prevFrame->slots;
    newFrame->isCCall = true;
    newFrame->nativeFunc = native;
    newFrame->file = EC->filename;
    BlockStackEntry *bentry = vec_last_or(&THREAD()->v_blockStack, NULL);
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
            throwErrorFmt(lxTypeErrClass, "Tried to invoke method on non-instance (type=%s)", typeOfObj(TO_OBJ(instance)));
        }
        frameClass = instance->klass; // TODO: make class the callable's class, not the instance class
    } else {
        // wrong usage of callCallable, callable should be on stack, below the arguments
        ASSERT(isCallable(*(EC->stackTop-argCount-1))); // should be same as `callable`
    }
    if (IS_CLOSURE(callable)) { // lox function
        closure = AS_CLOSURE(callable);
        if (!isMethod) {
            EC->stackTop[-argCount - 1] = callable; // should already be the callable, but just in case
        }
    } else if (IS_CLASS(callable)) { // initializer
        ObjClass *klass = AS_CLASS(callable);
        const char *klassName = NULL;
#ifndef NDEBUG
        klassName = className(klass);
#endif
        (void)klassName;
        VM_DEBUG(2, "calling callable class %s", klassName);
        instance = newInstance(klass, NEWOBJ_FLAG_NONE); // setup the new instance object
        frameClass = klass;
        instanceVal = OBJ_VAL(instance);
        volatile Obj *oldThis = th->thisObj;
        th->thisObj = TO_OBJ(instance);
        /*ASSERT(IS_CLASS(EC->stackTop[-argCount - 1])); this holds true if the # of args is correct for the function */
        EC->stackTop[-argCount - 1] = instanceVal; // first argument is instance, replaces class object
        // Call the initializer, if there is one.
        Value initializer;
        Obj *init = instanceFindMethod(instance, vm.initString);
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
                newFrame->instance = instance;
                newFrame->klass = frameClass;
                newFrame->callInfo = callInfo;
                VM_DEBUG(2, "calling native initializer for class %s with %d args", klassName, argCount);
                Value val = captureNativeError(nativeInit, argCount+1, EC->stackTop-argCount-1, callInfo);
                th = THREAD();
                th->thisObj = oldThis;
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
                    ASSERT(LIKELY(IS_INSTANCE_LIKE(val)));
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
            const char callableNameBuf[200];
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
        pushNativeFrame(native);
        volatile CallFrame *newFrame = getFrame();
        volatile VMExecContext *ec = EC;
        newFrame->instance = instance;
        newFrame->klass = frameClass;
        newFrame->callInfo = callInfo;
        volatile Value val = captureNativeError(native, argci, EC->stackTop-argci, callInfo);
        newFrame->slots = ec->stackTop - argcActuali;
        if (THREAD()->returnedFromNativeErr) {
            VM_DEBUG(2, "Returned from native function with error");
            THREAD()->returnedFromNativeErr = false;
            while (getFrame() >= newFrame) {
                popFrame();
            }
            THREAD()->inCCall = 0;
            VM_DEBUG(2, "Rethrowing inside VM");
            throwError(THREAD()->lastErrorThrown); // re-throw inside VM
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
                    } else {
                        // keyword argument not given, we need to add UNDEF_VAL to
                        // stack later
                    }
                }
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

    for (int i = numDefaultArgsUsed; i > 0; i--) {
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
            for (int i = numRestArgs; i > 0; i--) pop();
            push(restAry);
            argCountWithRestAry++;
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
        "defaultsUnused: %d, numRestArgs: %d, argCount: %d",
        func->arity, func->numDefaultArgs, numDefaultArgsUsed,
        numDefaultArgsUnused, numRestArgs, argCount
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

    if (callInfo && func->numKwargs > 0) {
        push(kwargsMap);
    }

    // add frame
    VM_DEBUG(2, "%s", "Pushing callframe (non-native)");
    CallFrame *frame = pushFrame();
    frame->instance = instance;
    frame->callInfo = callInfo;
    if (instance && !frameClass) frameClass = instance->klass;
    frame->klass = frameClass;
    if (funcOffset > 0) {
        VM_DEBUG(2, "Func offset due to optargs: %d", (int)funcOffset);
    }
    frame->closure = closure;
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
    // NOTE: the frame is popped on OP_RETURN
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
    int lenAfter = th->stackObjects.length;

    // allow collection of new stack-created objects if they're not rooted now
    for (int i = lenBefore; i < lenAfter; i++) {
        (void)vec_pop(&th->stackObjects);
    }

    return ret;
}

static Obj *findMethod(Obj *klass, ObjString *methodName) {
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
    LxThread *th = THREAD();
    CatchTable *tbl = currentChunk()->catchTbl;
    CatchTable *row = tbl;
    int currentIpOff = (int)(getFrame()->ip - currentChunk()->code);
    bool poppedEC = false;
    VM_DEBUG(2, "findthrowjumploc");
    while (row || EC->frameCount >= 1) {
        VM_DEBUG(2, "framecount: %d, num ECs: %d", EC->frameCount, th->v_ecs.length);
        if (row == NULL) { // pop a call frame
            VM_DEBUG(2, "row null");
            if (th->v_ecs.length == 0 || (th->v_ecs.length == 1 && EC->frameCount == 1)) {
                return false;
            }
            if (EC->frameCount == 1) { // there's at least 1 more context to go through
                pop_EC();
                poppedEC = true;
                ASSERT(EC->stackTop > getFrame()->slots);
                row = currentChunk()->catchTbl;
                continue;
            } else { // more frames in this context to go through
                ASSERT(EC->frameCount > 1);
                currentIpOff = getFrame()->start;
                ASSERT(EC->stackTop >= getFrame()->slots);
                EC->stackTop = getFrame()->slots;
                popFrame();
                VM_DEBUG(2, "frame popped");
                row = currentChunk()->catchTbl;
                continue;
            }
        }
        Value klassFound;
        if (!tableGet(&vm.globals, row->catchVal, &klassFound)) {
            VM_DEBUG(2, "a class not found for row, next row");
            row = row->next;
            continue;
        }
        VM_DEBUG(2, "a class found for row");
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

ErrTagInfo *findErrTag(ObjClass *klass) {
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
    char sbuf[250] = {'\0'};
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
    unsigned long tid = 0;
    if (th != vm.mainThread) {
        tid = th->tid;
    }
    if (th->ec->stackTop == th->ec->stack && th->v_ecs.length == 1) {
        fprintf(f, "[DEBUG %d (th=%lu)]: Stack: empty\n", th->vmRunLvl, tid);
        return;
    }
    VMExecContext *ec = NULL; int i = 0;
    int numCallFrames = VMNumCallFrames();
    int numStackFrames = VMNumStackFrames();
    fprintf(f, "[DEBUG %d (th=%lu)]: Stack (%d stack frames, %d call frames):\n", th->vmRunLvl,
            tid, numStackFrames, numCallFrames);
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
    if (th->openUpvalues == NULL) {
        th->openUpvalues = newUpvalue(local, NEWOBJ_FLAG_NONE);
        return th->openUpvalues;
    }

    if (GET_OPTION(debugVMLvl) >= 2) {
        VM_DEBUG(2, "Capturing upvalue: ");
        printValue(stderr, *local, false, -1);
        fprintf(stderr, "\n");
    }

    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = th->openUpvalues;

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
    ObjUpvalue* createdUpvalue = newUpvalue(local, NEWOBJ_FLAG_NONE);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        // The new one is the first one in the list.
        th->openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(Value *last) {
    LxThread *th = vm.curThread;
    while (th->openUpvalues != NULL && th->openUpvalues->value >= last) {
        ObjUpvalue *upvalue = th->openUpvalues;

        // Move the value into the upvalue itself and point the upvalue to it.
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
        UNREACHABLE("type: %s", typeOfVal(val)); // FIXME: throw typeerror
    }
}

static ObjString *methodNameForBinop(OpCode code) {
    switch (code) {
    case OP_ADD:
        return INTERNED("opAdd", 5);
    case OP_SUBTRACT:
        return INTERNED("opDiff", 6);
    case OP_MULTIPLY:
        return INTERNED("opMul", 5);
    case OP_DIVIDE:
        return INTERNED("opDiv", 5);
    case OP_SHOVEL_L:
        return INTERNED("opShovelLeft", 12);
    case OP_SHOVEL_R:
        return INTERNED("opShovelRight", 13);
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
    th->vmRunLvl++;
    register Chunk *ch = currentChunk();
    register Value *constantSlots = ch->constants->values;
    register CallFrame *frame = getFrame();
    if (ch->catchTbl != NULL) {
        int jumpRes = setjmp(frame->jmpBuf);
        if (jumpRes == JUMP_SET) {
            frame->jmpBufSet = true;
            VM_DEBUG(2, "VM set catch table for call frame (vm_run lvl %d)", th->vmRunLvl-1);
        } else {
            VM_DEBUG(2, "VM caught error for call frame (vm_run lvl %d)", th->vmRunLvl-1);
            th = THREAD();
            th->hadError = false;
            ch = currentChunk();
            constantSlots = ch->constants->values;
            frame = getFrame();
            // stack is already unwound to proper frame
        }
    }
#define READ_BYTE() (*(frame->ip++))
#define READ_CONSTANT() (constantSlots[READ_BYTE()])
#define BINARY_OP(op, opcode, type) \
    do { \
      Value b = peek(0);\
      Value a = peek(1);\
      if (IS_NUMBER(a) && IS_NUMBER(b)) {\
          if (UNLIKELY(((opcode == OP_DIVIDE || opcode == OP_MODULO) && AS_NUMBER(b) == 0.00))) {\
              throwErrorFmt(lxErrClass, "Can't divide by 0");\
          }\
          pop();\
          pushSwap(NUMBER_VAL((type)AS_NUMBER(a) op (type)AS_NUMBER(b)));\
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
              THREAD_DEBUG(2, "Releasing GVL after ops up %lu", pthread_self());
              releaseGVL();
              threadSleepNano(th, 100);
              acquireGVL();
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
        printVMStack(stderr, THREAD());
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
    vm.lastOp = instruction;
#endif

#ifdef COMPUTED_GOTO
    DISPATCH_TOP
#else
    switch (instruction) {
#endif
      CASE_OP(CONSTANT): { // numbers, code chunks
          Value constant = READ_CONSTANT();
          push(constant);
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
          Value val = peek(0);
          if (UNLIKELY(!IS_NUMBER(val))) {
              pop();
              throwErrorFmt(lxTypeErrClass, "Can only negate numbers, type=%s", typeOfVal(val));
          }
          pushSwap(NUMBER_VAL(-AS_NUMBER(val)));
          DISPATCH_BOTTOM();
      }
      CASE_OP(LESS): {
          Value rhs = pop(); // rhs
          Value lhs = peek(0); // lhs
          if (UNLIKELY(!canCmpValues(lhs, rhs, instruction))) {
              pop();
              throwErrorFmt(lxTypeErrClass,
                      "Can only compare 2 numbers or 2 strings with '<', lhs=%s, rhs=%s",
                      typeOfVal(lhs), typeOfVal(rhs));
          }
          if (cmpValues(lhs, rhs, instruction) == -1) {
              pushSwap(trueValue());
          } else {
              pushSwap(falseValue());
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(GREATER): {
        Value rhs = pop();
        Value lhs = peek(0);
        if (UNLIKELY(!canCmpValues(lhs, rhs, instruction))) {
            pop();
            throwErrorFmt(lxTypeErrClass,
                "Can only compare 2 numbers or 2 strings with '>', lhs=%s, rhs=%s",
                typeOfVal(lhs), typeOfVal(rhs));
        }
        if (cmpValues(lhs, rhs, instruction) == 1) {
            pushSwap(trueValue());
        } else {
            pushSwap(falseValue());
        }
        DISPATCH_BOTTOM();
      }
      CASE_OP(EQUAL): {
          Value rhs = pop();
          Value lhs = peek(0);
          if (isValueOpEqual(lhs, rhs)) {
              pushSwap(trueValue());
          } else {
              pushSwap(falseValue());
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(NOT_EQUAL): {
          Value rhs = pop();
          Value lhs = peek(0);
          if (isValueOpEqual(lhs, rhs)) {
              pushSwap(falseValue());
          } else {
              pushSwap(trueValue());
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(NOT): {
          Value val = peek(0);
          pushSwap(BOOL_VAL(!isTruthy(val)));
          DISPATCH_BOTTOM();
      }
      CASE_OP(GREATER_EQUAL): {
          Value rhs = pop();
          Value lhs = peek(0);
          if (UNLIKELY(!canCmpValues(lhs, rhs, instruction))) {
              pop();
              throwErrorFmt(lxTypeErrClass,
                  "Can only compare 2 numbers or 2 strings with '>=', lhs=%s, rhs=%s",
                   typeOfVal(lhs), typeOfVal(rhs));
          }
          if (cmpValues(lhs, rhs, instruction) != -1) {
              pushSwap(trueValue());
          } else {
              pushSwap(falseValue());
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(LESS_EQUAL): {
          Value rhs = pop();
          Value lhs = peek(0);
          if (UNLIKELY(!canCmpValues(lhs, rhs, instruction))) {
              pop();
              throwErrorFmt(lxTypeErrClass,
                  "Can only compare 2 numbers or 2 strings with '<=', lhs=%s, rhs=%s",
                   typeOfVal(lhs), typeOfVal(rhs));
          }
          if (cmpValues(lhs, rhs, instruction) != 1) {
              pushSwap(trueValue());
          } else {
              pushSwap(falseValue());
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(PRINT): {
          Value val = pop();
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
              pop();
              throwErrorFmt(lxNameErrClass, "Can't redeclare global variable '%s'", name);
          }
          Value val = peek(0);
          tableSet(&vm.globals, varName, val);
          pop();
          DISPATCH_BOTTOM();
      }
      CASE_OP(GET_GLOBAL): {
          Value varName = READ_CONSTANT();
          Value val;
          if (tableGet(&EC->roGlobals, varName, &val)) {
              push(val);
          } else if (tableGet(&vm.globals, varName, &val)) {
              push(val);
          } else {
              throwErrorFmt(lxNameErrClass, "Undefined global variable '%s'.", AS_STRING(varName)->chars);
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(SET_GLOBAL): {
          Value val = peek(0);
          Value varName = READ_CONSTANT();
          char *name = AS_CSTRING(varName);
          if (UNLIKELY(isUnredefinableGlobal(name))) {
              throwErrorFmt(lxNameErrClass, "Can't redefine global variable '%s'", name);
          }
          tableSet(&vm.globals, varName, val);
          DISPATCH_BOTTOM();
      }
      CASE_OP(NIL): {
          push(nilValue());
          DISPATCH_BOTTOM();
      }
      CASE_OP(TRUE): {
          push(BOOL_VAL(true));
          DISPATCH_BOTTOM();
      }
      CASE_OP(FALSE): {
          push(BOOL_VAL(false));
          DISPATCH_BOTTOM();
      }
      CASE_OP(AND): {
          Value rhs = pop();
          Value lhs = peek(0);
          (void)lhs;
          // NOTE: we only check truthiness of rhs because lhs is
          // short-circuited (a JUMP_IF_FALSE is output in the bytecode for
          // the lhs).
          pushSwap(isTruthy(rhs) ? rhs : BOOL_VAL(false));
          DISPATCH_BOTTOM();
      }
      CASE_OP(OR): {
          Value rhs = pop();
          Value lhs = peek(0);
          pushSwap(isTruthy(lhs) || isTruthy(rhs) ? rhs : lhs);
          DISPATCH_BOTTOM();
      }
      CASE_OP(POP): {
          pop();
          DISPATCH_BOTTOM();
      }
      CASE_OP(SET_LOCAL): {
          uint8_t slot = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          ASSERT(slot >= 0);
          frame->slots[slot] = peek(0); // locals are popped at end of scope by VM
          DISPATCH_BOTTOM();
      }
      CASE_OP(UNPACK_SET_LOCAL): {
          uint8_t slot = READ_BYTE();
          uint8_t unpackIdx = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          ASSERT(slot >= 0);
          // make sure we don't clobber the unpack array with the setting of
          // this variable
          int peekIdx = 0;
          while (frame->slots+slot > EC->stackTop-1) {
              push(NIL_VAL);
              peekIdx++;
          }
          frame->slots[slot] = unpackValue(peek(peekIdx+unpackIdx), unpackIdx); // locals are popped at end of scope by VM
          DISPATCH_BOTTOM();
      }
      CASE_OP(GET_LOCAL): {
          uint8_t slot = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          ASSERT(slot >= 0);
          push(frame->slots[slot]);
          DISPATCH_BOTTOM();
      }
      CASE_OP(GET_UPVALUE): {
          uint8_t slot = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          push(*frame->closure->upvalues[slot]->value);
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
          closeUpvalues(EC->stackTop - 1);
          pop(); // pop the variable from the stack frame
          DISPATCH_BOTTOM();
      }
      CASE_OP(CLOSURE): {
          Value funcVal = READ_CONSTANT();
          ASSERT(IS_FUNCTION(funcVal));
          ObjFunction *func = AS_FUNCTION(funcVal);
          bool prevGc = turnGCOff();
          ObjClosure *closure = newClosure(func, NEWOBJ_FLAG_NONE);
          push(OBJ_VAL(closure));

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
          setGCOnOff(prevGc);
          DISPATCH_BOTTOM();
      }
      CASE_OP(JUMP_IF_FALSE): {
          Value cond = pop();
          uint8_t ipOffset = READ_BYTE();
          if (!isTruthy(cond)) {
              DBG_ASSERT(ipOffset > 0);
              frame->ip += (ipOffset-1);
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(JUMP_IF_TRUE): {
          Value cond = pop();
          uint8_t ipOffset = READ_BYTE();
          if (isTruthy(cond)) {
              DBG_ASSERT(ipOffset > 0);
              frame->ip += (ipOffset-1);
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(JUMP_IF_FALSE_PEEK): {
          Value cond = peek(0);
          uint8_t ipOffset = READ_BYTE();
          if (!isTruthy(cond)) {
              DBG_ASSERT(ipOffset > 0);
              frame->ip += (ipOffset-1);
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(JUMP_IF_TRUE_PEEK): {
          Value cond = peek(0);
          uint8_t ipOffset = READ_BYTE();
          if (isTruthy(cond)) {
              DBG_ASSERT(ipOffset > 0);
              frame->ip += (ipOffset-1);
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(JUMP): {
          uint8_t ipOffset = READ_BYTE();
          ASSERT(ipOffset > 0);
          frame->ip += (ipOffset-1);
          DISPATCH_BOTTOM();
      }
      CASE_OP(LOOP): {
          uint8_t ipOffset = READ_BYTE();
          ASSERT(ipOffset > 0);
          // add 1 for the instruction we just read, and 1 to go 1 before the
          // instruction we want to execute next.
          frame->ip -= (ipOffset+2);
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
          Value ret = pop();
          Value err = newError(lxReturnBlockErrClass, NIL_VAL);
          setProp(err, key, ret);
          throwError(err); // blocks catch this, not propagated
          DISPATCH_BOTTOM();
      }
      CASE_OP(TO_BLOCK): {
          Value func = peek(0);
          if (UNLIKELY(!isCallable(func))) {
              pop();
              throwErrorFmt(lxTypeErrClass, "Cannot use '&' operator on a non-function");
          }
          pushSwap(newBlock(AS_OBJ(func)));
          DISPATCH_BOTTOM();
      }
      CASE_OP(CALL): {
          uint8_t numArgs = READ_BYTE();
          if (th->lastSplatNumArgs > 0) {
              numArgs += (th->lastSplatNumArgs-1);
              th->lastSplatNumArgs = -1;
          }
          Value callableVal = peek(numArgs);
          if (UNLIKELY(!isCallable(callableVal))) {
              for (int i = 0; i < numArgs; i++) {
                  pop();
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
          Value kwMap = peek(0);
          ASSERT(IS_T_MAP(kwMap));
          uint8_t kwSlot = READ_BYTE();
          uint8_t mapSlot = READ_BYTE();
          (void)mapSlot; // unused
          if (IS_UNDEF(getFrame()->slots[kwSlot])) {
              push(BOOL_VAL(false));
          } else {
              push(BOOL_VAL(true));
          }
          DISPATCH_BOTTOM();
      }
      CASE_OP(INVOKE): { // invoke methods (includes static methods)
          Value methodName = READ_CONSTANT();
          ObjString *mname = AS_STRING(methodName);
          uint8_t numArgs = READ_BYTE();
          Value callInfoVal = READ_CONSTANT();
          Obj *oldThis = th->thisObj;
          CallInfo *callInfo = internalGetData(AS_INTERNAL(callInfoVal));
          if (th->lastSplatNumArgs > 0) {
              numArgs += (th->lastSplatNumArgs-1);
              th->lastSplatNumArgs = -1;
          }
          Value instanceVal = peek(numArgs);
          if (IS_INSTANCE(instanceVal) || IS_ARRAY(instanceVal) || IS_STRING(instanceVal) || IS_MAP(instanceVal)) {
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
              setThis(numArgs);
              callCallable(OBJ_VAL(callable), numArgs, true, callInfo);
              th->thisObj = oldThis;
          } else if (IS_CLASS(instanceVal)) {
              ObjClass *klass = AS_CLASS(instanceVal);
              Obj *callable = classFindStaticMethod(klass, mname);
              /*if (!callable && numArgs == 0) {*/
                  /*callable = instanceFindGetter((ObjInstance*)klass, mname);*/
              /*}*/
              if (UNLIKELY(!callable)) {
                  ObjString *className = CLASSINFO(klass)->name;
                  const char *classStr = className ? className->chars : "(anon)";
                  throwErrorFmt(lxErrClass, "class method '%s.%s' not found", classStr, mname->chars);
              }
              EC->stackTop[-numArgs-1] = instanceVal;
              setThis(numArgs);
              callCallable(OBJ_VAL(callable), numArgs, true, callInfo);
              th->thisObj = oldThis;
          } else if (IS_MODULE(instanceVal)) {
              ObjModule *mod = AS_MODULE(instanceVal);
              Obj *callable = classFindStaticMethod((ObjClass*)mod, mname);
              /*if (!callable && numArgs == 0) {*/
                  /*callable = instanceFindGetter((ObjInstance*)mod, mname);*/
              /*}*/
              if (UNLIKELY(!callable)) {
                  ObjString *modName = CLASSINFO(mod)->name;
                  const char *modStr = modName ? modName->chars : "(anon)";
                  throwErrorFmt(lxErrClass, "module method '%s.%s' not found", modStr, mname->chars);
              }
              EC->stackTop[-numArgs-1] = instanceVal;
              setThis(numArgs);
              callCallable(OBJ_VAL(callable), numArgs, true, callInfo);
              th->thisObj = oldThis;
          } else {
              throwErrorFmt(lxTypeErrClass, "Tried to invoke method on non-instance (type=%s)", typeOfVal(instanceVal));
          }
          ASSERT_VALID_STACK();
          DISPATCH_BOTTOM();
      }
      CASE_OP(GET_THIS): {
          ASSERT(th->thisObj);
          push(OBJ_VAL(th->thisObj));
          DISPATCH_BOTTOM();
      }
      CASE_OP(SPLAT_ARRAY): {
          Value ary = pop();
          if (UNLIKELY(!IS_AN_ARRAY(ary))) {
              throwErrorFmt(lxTypeErrClass, "Splatted expression must evaluate to an Array (type=%s)",
                      typeOfVal(ary));
          }
          th->lastSplatNumArgs = ARRAY_SIZE(ary);
          for (int i = 0; i < th->lastSplatNumArgs; i++) {
              push(ARRAY_GET(ary, i));
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
          push(OBJ_VAL(bmethod));
          DISPATCH_BOTTOM();
      }
      // return from function/method, and close all upvalues in the callframe frame
      CASE_OP(RETURN): {
          // this is if we're in a block given by (&block), and we returned
          // (explicitly or implicitly)
          if (UNLIKELY(th->v_blockStack.length > 0)) {
              ObjString *key = INTERN("ret");
              pop();
              Value ret;
              if (th->lastValue) {
                  ret = *th->lastValue;
              } else {
                  ret = NIL_VAL;
              }
              Value err = newError(lxContinueBlockErrClass, NIL_VAL);
              setProp(err, key, ret);
              throwError(err); // blocks catch this, not propagated
              (th->vmRunLvl)--;
              DISPATCH_BOTTOM();
          }
          Value result = pop(); // pop from caller's frame
          ASSERT(!getFrame()->isCCall);
          Value *newTop = getFrame()->slots;
          closeUpvalues(getFrame()->slots);
          popFrame();
          EC->stackTop = newTop;
          push(result);
          (th->vmRunLvl)--;
          return INTERPRET_OK;
      }
      CASE_OP(ITER): {
          Value iterable = peek(0);
          if (UNLIKELY(!isIterableType(iterable))) {
              throwErrorFmt(lxTypeErrClass, "Non-iterable value given to 'foreach' statement. Type found: %s",
                      typeOfVal(iterable));
          }
          Value iterator = createIterator(iterable);
          DBG_ASSERT(isIterator(iterator));
          DBG_ASSERT(isIterableType(peek(0)));
          pushSwap(iterator);
          DISPATCH_BOTTOM();
      }
      CASE_OP(ITER_NEXT): {
          Value iterator = peek(0);
          ASSERT(isIterator(iterator));
          Value next = iteratorNext(iterator);
          ASSERT(!IS_UNDEF(next));
          push(next);
          DISPATCH_BOTTOM();
      }
      CASE_OP(CLASS): { // add or re-open class
          Value className = READ_CONSTANT();
          Value existingClass;
          // FIXME: not perfect, if class is declared non-globally this won't
          // detect it. Maybe a new op-code is needed for class re-opening.
          if (tableGet(&vm.globals, className, &existingClass)) {
              if (IS_CLASS(existingClass)) { // re-open class
                  push(existingClass);
                  DISPATCH_BOTTOM();
              } else if (UNLIKELY(IS_MODULE(existingClass))) {
                  const char *classStr = AS_CSTRING(className);
                  throwErrorFmt(lxTypeErrClass, "Tried to define class %s, but it's a module",
                          classStr);
              } // otherwise we override the global var with the new class
          }
          ObjClass *klass = newClass(AS_STRING(className), lxObjClass, NEWOBJ_FLAG_OLD);
          push(OBJ_VAL(klass));
          setThis(0);
          DISPATCH_BOTTOM();
      }
      CASE_OP(MODULE): { // add or re-open module
          Value modName = READ_CONSTANT();
          Value existingMod;
          // FIXME: not perfect, if class is declared non-globally this won't
          // detect it. Maybe a new op-code is needed for class re-opening.
          if (tableGet(&vm.globals, modName, &existingMod)) {
              if (IS_MODULE(existingMod)) {
                push(existingMod); // re-open the module
                DISPATCH_BOTTOM();
              } else if (UNLIKELY(IS_CLASS(existingMod))) {
                  const char *modStr = AS_CSTRING(modName);
                  throwErrorFmt(lxTypeErrClass, "Tried to define module %s, but it's a class",
                          modStr);
              } // otherwise, we override the global var with the new module
          }
          ObjModule *mod = newModule(AS_STRING(modName), NEWOBJ_FLAG_OLD);
          push(OBJ_VAL(mod));
          setThis(0);
          DISPATCH_BOTTOM();
      }
      CASE_OP(SUBCLASS): { // add new class inheriting from an existing class
          Value className = READ_CONSTANT();
          Value superclass =  pop();
          if (!IS_CLASS(superclass)) {
              throwErrorFmt(lxTypeErrClass,
                      "Class %s tried to inherit from non-class",
                      AS_CSTRING(className)
              );
          }
          // FIXME: not perfect, if class is declared non-globally this won't detect it.
          Value existingClass;
          if (tableGet(&vm.globals, className, &existingClass)) {
              if (UNLIKELY(IS_CLASS(existingClass))) {
                  throwErrorFmt(lxNameErrClass, "Class %s already exists (if "
                          "re-opening class, no superclass should be given)",
                          AS_CSTRING(className));
              } else if (UNLIKELY(IS_MODULE(existingClass))) {
                  throwErrorFmt(lxTypeErrClass, "Tried to define class %s, but it's a module", AS_CSTRING(className));
              }
          }
          ObjClass *klass = newClass(
              AS_STRING(className),
              AS_CLASS(superclass),
              NEWOBJ_FLAG_OLD
          );
          push(OBJ_VAL(klass));
          setThis(0);
          DISPATCH_BOTTOM();
      }
      CASE_OP(IN): {
          Value classOrInst = pop();
          if (IS_CLASS(classOrInst) || IS_MODULE(classOrInst)) {
              push(classOrInst);
          } else {
              if (!IS_INSTANCE(classOrInst)) {
                  throwErrorFmt(lxTypeErrClass, "Expression given to 'in' statement "
                          "must evaluate to a class/module/instance (type=%s)", typeOfVal(classOrInst));
              }
              ObjClass *klass = instanceSingletonClass(AS_INSTANCE(classOrInst));
              push(OBJ_VAL(klass));
          }
          setThis(0);
          DISPATCH_BOTTOM();
      }
      CASE_OP(METHOD): { // method definition in class or module
          Value methodName = READ_CONSTANT();
          ObjString *methStr = AS_STRING(methodName);
          defineMethod(methStr);
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
          ASSERT(propStr && propStr->chars);
          Value instance = peek(0);
          if (UNLIKELY(!IS_INSTANCE_LIKE(instance))) {
              pop();
              throwErrorFmt(lxTypeErrClass, "Tried to access property '%s' of non-instance (type: %s)", propStr->chars, typeOfVal(instance));
          }
          Value val = propertyGet(AS_INSTANCE(instance), propStr);
          pushSwap(val);
          DISPATCH_BOTTOM();
      }
      CASE_OP(PROP_SET): {
          Value propName = READ_CONSTANT();
          ObjString *propStr = AS_STRING(propName);
          Value rval = peek(0);
          Value instance = peek(1);
          if (UNLIKELY(!IS_INSTANCE_LIKE(instance))) {
              pop(); pop();
              throwErrorFmt(lxTypeErrClass, "Tried to set property '%s' of non-instance", propStr->chars);
          }
          propertySet(AS_INSTANCE(instance), propStr, rval); // TODO: check frozenness of object
          pop(); // leave rval on stack
          pushSwap(rval);
          DISPATCH_BOTTOM();
      }
      CASE_OP(INDEX_GET): {
          Value lval = peek(1); // ex: Array/String/instance object
          if (UNLIKELY(!IS_INSTANCE_LIKE(lval))) {
              throwErrorFmt(lxTypeErrClass, "Cannot call opIndexGet ('[]') on a non-instance, found a: %s", typeOfVal(lval));
          }
          ObjInstance *instance = AS_INSTANCE(lval);
          Obj *method = instanceFindMethodOrRaise(instance, INTERNED("opIndexGet", 10));
          callCallable(OBJ_VAL(method), 1, true, NULL);
          DISPATCH_BOTTOM();
      }
      CASE_OP(INDEX_SET): {
          Value lval = peek(2);
          if (UNLIKELY(!IS_INSTANCE_LIKE(lval))) {
              throwErrorFmt(lxTypeErrClass, "Cannot call opIndexSet ('[]=') on a non-instance, found a: %s", typeOfVal(lval));
          }
          ObjInstance *instance = AS_INSTANCE(lval);
          Obj *method = instanceFindMethodOrRaise(instance, INTERNED("opIndexSet", 10));
          callCallable(OBJ_VAL(method), 2, true, NULL);
          DISPATCH_BOTTOM();
      }
      CASE_OP(THROW): {
          Value throwable = pop();
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
          UNREACHABLE("after throw");
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
          push(tblRow->lastThrownValue);
          DISPATCH_BOTTOM();
      }
      CASE_OP(STRING): {
          Value strLit = READ_CONSTANT();
          DBG_ASSERT(IS_STRING(strLit));
          uint8_t isStatic = READ_BYTE();
          push(OBJ_VAL(lxStringClass));
          ObjString *buf = AS_STRING(strLit);
          buf->klass = lxStringClass;
          if (UNLIKELY(isStatic)) {
              STRING_SET_STATIC(buf);
              push(OBJ_VAL(buf));
          } else {
              push(OBJ_VAL(buf));
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
              Value el = pop();
              writeValueArrayEnd(ary, el);
          }
          push(aryVal);
          unhideFromGC(AS_OBJ(aryVal));
          DISPATCH_BOTTOM();
      }
      CASE_OP(DUPARRAY): {
          Value ary = READ_CONSTANT();
          AS_ARRAY(ary)->klass = lxAryClass; // NOTE: this is actually needed
          DBG_ASSERT(IS_AN_ARRAY(ary));
          push(arrayDup(ary));
          DISPATCH_BOTTOM();
      }
      CASE_OP(MAP): {
          uint8_t numKeyVals = READ_BYTE();
          DBG_ASSERT(numKeyVals % 2 == 0);
          Value mapVal = newMap();
          Table *map = AS_MAP(mapVal)->table;
          for (int i = 0; i < numKeyVals; i+=2) {
              Value key = pop();
              Value val = pop();
              tableSet(map, key, val);
              OBJ_WRITE(mapVal, key);
              OBJ_WRITE(mapVal, val);
          }
          push(mapVal);
          DISPATCH_BOTTOM();
      }
      CASE_OP(DUPMAP): {
          Value map = READ_CONSTANT();
          DBG_ASSERT(IS_A_MAP(map));
          push(mapDup(map));
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

InterpretResult interpret(Chunk *chunk, char *filename) {
    ASSERT(chunk);
    if (!EC) {
        return INTERPRET_UNINITIALIZED; // call initVM() first!
    }
    EC->filename = copyString(filename, strlen(filename), NEWOBJ_FLAG_OLD);
    EC->frameCount = 0;
    VM_DEBUG(1, "%s", "Pushing initial callframe");
    CallFrame *frame = pushFrame();
    frame->start = 0;
    frame->ip = chunk->code;
    frame->slots = EC->stack;
    ObjFunction *func = newFunction(chunk, NULL, NEWOBJ_FLAG_OLD);
    hideFromGC(TO_OBJ(func));
    frame->closure = newClosure(func, NEWOBJ_FLAG_OLD);
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
InterpretResult loadScript(Chunk *chunk, char *filename) {
    ASSERT(chunk);
    CallFrame *oldFrame = getFrame();
    push_EC();
    resetStack();
    VMExecContext *ectx = EC;
    EC->loadContext = true;
    EC->filename = copyString(filename, strlen(filename), NEWOBJ_FLAG_OLD);
    VM_DEBUG(1, "%s", "Pushing initial callframe");
    CallFrame *frame = pushFrame();
    frame->start = 0;
    frame->ip = chunk->code;
    frame->slots = EC->stack;
    ObjFunction *func = newFunction(chunk, NULL, NEWOBJ_FLAG_OLD);
    hideFromGC(TO_OBJ(func));
    frame->closure = newClosure(func, NEWOBJ_FLAG_OLD);
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

static Value doVMEval(const char *src, const char *filename, int lineno, bool throwOnErr) {
    CallFrame *oldFrame = getFrame();
    CompileErr err = COMPILE_ERR_NONE;
    int oldOpts = compilerOpts.noRemoveUnusedExpressions;
    compilerOpts.noRemoveUnusedExpressions = true;
    push_EC();
    VMExecContext *ectx = EC;
    ectx->evalContext = true;
    resetStack();
    Chunk *chunk = compile_src(src, &err);
    compilerOpts.noRemoveUnusedExpressions = oldOpts;

    if (err != COMPILE_ERR_NONE || !chunk) {
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
    EC->filename = copyString(filename, strlen(filename), NEWOBJ_FLAG_OLD);
    VM_DEBUG(1, "%s", "Pushing initial eval callframe");
    CallFrame *frame = pushFrame();
    frame->start = 0;
    frame->ip = chunk->code;
    frame->slots = EC->stack;
    ObjFunction *func = newFunction(chunk, NULL, NEWOBJ_FLAG_OLD);
    hideFromGC(TO_OBJ(func));
    frame->closure = newClosure(func, NEWOBJ_FLAG_OLD);
    unhideFromGC(TO_OBJ(func));
    frame->isCCall = false;
    frame->nativeFunc = NULL;

    setupPerScriptROGlobals(filename);

    ErrTag status = TAG_NONE;
    InterpretResult result = INTERPRET_OK;
    vm_protect(vm_run_protect, NULL, NULL, &status);
    if (status == TAG_RAISE) {
        result = INTERPRET_RUNTIME_ERROR;
        THREAD()->hadError = true;
    }
    Value val = *THREAD()->lastValue;
    VM_DEBUG(2, "eval finished: error: %d", THREAD()->hadError ? 1 : 0);
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

Value VMEvalNoThrow(const char *src, const char *filename, int lineno) {
    return doVMEval(src, filename, lineno, false);
}

Value VMEval(const char *src, const char *filename, int lineno) {
    return doVMEval(src, filename, lineno, true);
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
        /*if (th->errInfo->bentry) {*/
            /*fprintf(stderr, "Popping block entry (unwind)\n");*/
            /*popBlockEntry(th->errInfo->bentry);*/
        /*}*/
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
    addErrInfo(errClass);
    volatile ErrTagInfo *errInfo = th->errInfo;
    int jmpres = 0;
    if ((jmpres = setjmp(errInfo->jmpBuf)) == JUMP_SET) {
        *status = TAG_NONE;
        VM_DEBUG(2, "vm_protect before func");
        void *res = func(arg);
        ErrTagInfo *prev = errInfo->prev;
        FREE(ErrTagInfo, errInfo);
        th->errInfo = prev;
        VM_DEBUG(2, "vm_protect after func");
        return res;
    } else if (jmpres == JUMP_PERFORMED) {
        VM_DEBUG(2, "vm_protect got to longjmp");
        th = THREAD();
        ASSERT(errInfo == th->errInfo);
        unwindJumpRecover(errInfo);
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

static void detachUnjoinedThreads() {
    ObjInstance *threadInst = NULL; int tidx = 0;
    vec_foreach(&vm.threads, threadInst, tidx) {
        LxThread *th = (LxThread*)threadInst->internal->data;
        if (th == vm.mainThread) continue;
        if (!th->detached && th->status != THREAD_ZOMBIE && th->status != THREAD_KILLED && th->tid != -1) {
            THREAD_DEBUG(1, "Main thread detaching unjoined thread %lu due to exit", th->tid);
            threadDetach(th);
        }
    }
}

// TODO: rename to exitThread(), as this either stops the VM or exits the
// current non-main thread
NORETURN void stopVM(int status) {
    LxThread *th = vm.curThread;
    if (th == vm.mainThread) {
        THREAD_DEBUG(1, "Main thread exiting with %d", status);
        detachUnjoinedThreads();
        runAtExitHooks();
        freeVM();
        if (GET_OPTION(profileGC)) {
            printGCProfile();
        }
        vm.exited = true;
        pthread_exit(NULL);
    } else {
        exitingThread(th);
        th->exitStatus = status;
        if (th->detached && vm.numDetachedThreads > 0) {
            THREAD_DEBUG(1, "Thread %lu [DETACHED] exiting with %d", th->tid, status);
            vm.numDetachedThreads--;
        } else {
            THREAD_DEBUG(1, "Thread %lu exiting with %d", th->tid, status);
        }
        releaseGVL();
        pthread_exit(NULL);
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
    vm.GVLWaiters++;
    while (vm.GVLockStatus > 0) {
        pthread_cond_wait(&vm.GVLCond, &vm.GVLock); // block on wait queue
    }
    vm.GVLWaiters--;
    vm.GVLockStatus = 1;
    vm.curThread = FIND_THREAD(pthread_self());
    if (vm.curThread) {
        vm.curThread->opsRemaining = THREAD_OPS_UNTIL_SWITCH;
    }
    GVLOwner = pthread_self();
    pthread_mutex_unlock(&vm.GVLock);
    if (vm.curThread && !(IS_NIL(vm.curThread->errorToThrow))) {
        Value err = vm.curThread->errorToThrow;
        vm.curThread->errorToThrow = NIL_VAL;
        throwError(err);
    }

}

void releaseGVL(void) {
    pthread_mutex_lock(&vm.GVLock);
    LxThread *th = vm.curThread;
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
    GVLOwner = -1;
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
    vec_foreach(&vm.threads, threadInstance, thIdx) {
        th = (LxThread*)threadInstance->internal->data;
        ASSERT(th);
        if (th->tid == tid) return th;
    }
    return NULL;
}

ObjInstance *FIND_THREAD_INSTANCE(pthread_t tid) {
    ObjInstance *threadInstance; int thIdx = 0;
    LxThread *th = NULL;
    vec_foreach(&vm.threads, threadInstance, thIdx) {
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
