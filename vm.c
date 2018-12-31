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
#define VM_DEBUG(...) vm_debug(__VA_ARGS__)
#define VM_WARN(...) vm_warn(__VA_ARGS__)
#else
#define VM_DEBUG(...) (void)0
#define VM_WARN(...) (void)0
#endif

static void vm_debug(const char *format, ...) {
    if (!CLOX_OPTION_T(debugVM)) return;
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

Value lxLoadPath; // load path for loadScript/requireScript (-L flag)
Value lxArgv;

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
    nativeObjectInit = addNativeMethod(objClass, "init", lxObjectInit);
    addNativeMethod(objClass, "dup", lxObjectDup);
    addNativeMethod(objClass, "extend", lxObjectExtend);
    addNativeMethod(objClass, "hashKey", lxObjectHashKey);
    addNativeMethod(objClass, "opEquals", lxObjectOpEquals);
    addNativeMethod(objClass, "freeze", lxObjectFreeze);
    addNativeMethod(objClass, "unfreeze", lxObjectUnfreeze);
    addNativeMethod(objClass, "isFrozen", lxObjectIsFrozen);
    addNativeGetter(objClass, "class", lxObjectGetClass);
    addNativeGetter(objClass, "singletonClass", lxObjectGetSingletonClass);
    addNativeGetter(objClass, "objectId", lxObjectGetObjectId);
    lxObjClass = objClass;

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

    // module GC
    ObjModule *GCModule = addGlobalModule("GC");
    ObjClass *GCClassStatic = moduleSingletonClass(GCModule);
    addNativeMethod(GCClassStatic, "stats", lxGCStats);
    addNativeMethod(GCClassStatic, "collect", lxGCCollect);
    addNativeMethod(GCClassStatic, "on", lxGCOn);
    addNativeMethod(GCClassStatic, "off", lxGCOff);
    addNativeMethod(GCClassStatic, "setFinalizer", lxGCSetFinalizer);
    lxGCModule = GCModule;

    // order of initialization not important here
    Init_ProcessModule();
    Init_IOClass();
    Init_FileClass();
    Init_ThreadClass();
    isClassHierarchyCreated = true;
}

// NOTE: this initialization function can create Lox runtime objects
static void defineGlobalVariables(void) {
    lxLoadPath = newArray();
    ObjString *loadPathStr = internedString("loadPath", 8);
    ASSERT(tableSet(&vm.globals, OBJ_VAL(loadPathStr), lxLoadPath));
    // populate load path from -L option given to commandline
    char *lpath = GET_OPTION(initialLoadPath);
    if (lpath && strlen(lpath) > 0) {
        char *beg = lpath;
        char *end = NULL;
        while ((end = strchr(beg, ':'))) {
            ObjString *str = copyString(beg, end - beg);
            arrayPush(lxLoadPath, newStringInstance(str));
            beg = end+1;
        }
    }

    ObjString *argvStr = internedString("ARGV", 4);
    lxArgv = newArray();
    ASSERT(tableSet(&vm.globals, OBJ_VAL(argvStr), lxArgv));
    for (int i = 0; i < origArgc; i++) {
        ObjString *argStr = copyString(origArgv[i], strlen(origArgv[i]));
        Value arg = newStringInstance(argStr);
        hideFromGC(AS_OBJ(arg));
        arrayPush(lxArgv, arg);
        // FIXME: for some reason, GC fails sometimes when we don't hide this.
        // Really not sure why, we are marking vm.globals.
        unhideFromGC(AS_OBJ(arg));
    }
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
        ObjInstance *iterObj = newInstance(lxIteratorClass);
        callVMMethod(iterObj, OBJ_VAL(nativeIteratorInit), 1, &iterable);
        return pop();
    } else if (IS_INSTANCE(iterable)) {
        ObjString *iterId = internedString("iter", 4);
        ObjInstance *instance = AS_INSTANCE(iterable);
        Obj *method = instanceFindMethodOrRaise(instance, iterId);
        callVMMethod(instance, OBJ_VAL(method), 0, NULL);
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
    th->ec = (VMExecContext*)vec_last(&th->v_ecs);
    if (th->v_ecs.length == 0) {
        th->ec = NULL;
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
    if (pthread_cond_init(&vm.GVCond, NULL) != 0) {
        die("Global VM lock cond unable to initialize");
    }

    vm.GVLockStatus = 0;
    vm.curThread = NULL;
    vm.mainThread = NULL;

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
    VM_DEBUG("initVM() start");
    turnGCOff();
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    initTable(&vm.globals);
    initTable(&vm.strings); // interned strings

    vec_init(&vm.hiddenObjs);
    ObjInstance *mainT = initMainThread();
    push_EC();
    resetStack();

    vec_init(&vm.loadedScripts);
    vm.openUpvalues = NULL;
    vm.printBuf = NULL;
    curLine = 1;

    memset(&rootVMLoopJumpBuf, 0, sizeof(rootVMLoopJumpBuf));
    rootVMLoopJumpBufSet = false;

    vec_init(&vm.exitHandlers);

    initDebugger(&vm.debugger);
    vm.instructionStepperOn = CLOX_OPTION_T(stepVMExecution);

    vm.inited = true; // NOTE: VM has to be inited before creation of strings
    vm.exited = false;
    vm.initString = internedString("init", 4);
    vm.fileString = internedString("__FILE__", 8);
    vm.dirString = internedString("__DIR__", 7);

    pushFrame();

    defineNativeFunctions();
    defineNativeClasses();
    mainT->klass = lxThreadClass; // now that it's created
    defineGlobalVariables();

    popFrame();

    resetStack();
    // don't pop EC here, we'll pop it in freeVM()

    turnGCOn();
    VM_DEBUG("initVM() end");
}

void freeVM(void) {
    /*fprintf(stderr, "VM run level: %d\n", th->vmRunLvl);*/
    if (!vm.inited) {
        VM_WARN("freeVM: VM not yet initialized");
        return;
    }
    VM_DEBUG("freeVM() start");

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
    vm.openUpvalues = NULL;
    vec_deinit(&vm.hiddenObjs);
    vec_deinit(&vm.loadedScripts);
    isClassHierarchyCreated = false;

    vm.inited = false;
    vm.exited = false;

    vec_deinit(&vm.exitHandlers);

    while (THREAD()->ec) { pop_EC(); }

    releaseGVL();

    THREAD_DEBUG(1, "VM lock destroying...");
    pthread_mutex_destroy(&vm.GVLock);

    vm.curThread = NULL;
    vm.mainThread = NULL;
    vec_deinit(&vm.threads);

    VM_DEBUG("freeVM() end");
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

#define ASSERT_VALID_STACK() ASSERT(EC->stackTop >= EC->stack)

static bool isOpStackEmpty(void) {
    ASSERT_VALID_STACK();
    return EC->stackTop == EC->stack;
}

void push(Value value) {
    ASSERT_VALID_STACK();
    if (IS_OBJ(value)) {
        ASSERT(AS_OBJ(value)->type != OBJ_T_NONE);
    }
    *EC->stackTop = value;
    EC->stackTop++;
}

Value pop(void) {
    ASSERT(EC->stackTop > EC->stack);
    EC->stackTop--;
    EC->lastValue = EC->stackTop;
    THREAD()->lastValue = EC->lastValue;
    return *(THREAD()->lastValue);
}

Value peek(unsigned n) {
    ASSERT((EC->stackTop-n) > EC->stack);
    return *(EC->stackTop-1-n);
}

static inline void setThis(unsigned n) {
    ASSERT((EC->stackTop-n) > EC->stack);
    THREAD()->thisObj = AS_OBJ(*(EC->stackTop-1-n));
}

Value *getLastValue(void) {
    if (isOpStackEmpty()) {
        return EC->lastValue;
    } else {
        return EC->stackTop-1;
    }
}

static inline Value nilValue(void) {
    return NIL_VAL;
}

static inline Value trueValue(void) {
    return BOOL_VAL(true);
}

static inline Value falseValue(void) {
    return BOOL_VAL(false);
}

static bool isTruthy(Value val) {
    switch (val.type) {
    case VAL_T_NIL: return false;
    case VAL_T_BOOL: return AS_BOOL(val);
    case VAL_T_UNDEF: UNREACHABLE("undefined value found?");
    default:
        // all other values are truthy
        return true;

    }
}

static inline bool canCmpValues(Value lhs, Value rhs, uint8_t cmpOp) {
    return (IS_NUMBER(lhs) && IS_NUMBER(rhs)) ||
        (IS_A_STRING(lhs) && IS_A_STRING(rhs));
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
    } else if (IS_A_STRING(lhs) && IS_A_STRING(rhs)) {
        ObjString *lhsStr = VAL_TO_STRING(lhs);
        ObjString *rhsStr = VAL_TO_STRING(rhs);
        if (lhsStr->hash > 0 && rhsStr->hash > 0) {
            return lhsStr->hash == rhsStr->hash;
        } else {
            return strcmp(lhsStr->chars, rhsStr->chars);
        }
    }

    UNREACHABLE_RETURN(-2);
}

static bool isValueOpEqual(Value lhs, Value rhs) {
    if (lhs.type != rhs.type) {
        return false;
    }
    if (IS_OBJ(lhs)) {
        if (IS_INSTANCE_LIKE(lhs)) {
            ObjString *opEquals = internedString("opEquals", 8);
            ObjInstance *self = AS_INSTANCE(lhs);
            Obj *methodOpEq = instanceFindMethod(self, opEquals);
            if (methodOpEq) {
                Value ret = callVMMethod(self, OBJ_VAL(methodOpEq), 1, &rhs);
                pop();
                return isTruthy(ret);
            }
        }
        // 2 objects, same pointers to Obj are equal
        return AS_OBJ(lhs) == AS_OBJ(rhs);
    } else if (IS_NUMBER(lhs)) { // 2 numbers, same values are equal
        return AS_NUMBER(lhs) == AS_NUMBER(rhs);
    } else if (IS_NIL(lhs)) { // 2 nils, are equal
        return true;
    } else if (IS_BOOL(lhs)) {
        return AS_BOOL(lhs) == AS_BOOL(rhs);
    } else {
        return false;
    }
}

static inline CallFrame *getFrame(void) {
    ASSERT(EC->frameCount >= 1);
    return &EC->frames[EC->frameCount-1];
}

void debugFrame() {
    CallFrame *frame = getFrame();
    const char *fnName = frame->isCCall ? frame->nativeFunc->name->chars :
        frame->closure->function->name->chars;
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
    Value msg = getProp(err, internedString("message", 7));
    char *msgStr = NULL;
    if (!IS_NIL(msg)) {
        msgStr = VAL_TO_STRING(msg)->chars;
    }
    Value bt = getProp(err, internedString("backtrace", 9));
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

// every new error value, when thrown, gets its backtrace set first
void setBacktrace(Value err) {
    VM_DEBUG("Setting backtrace");
    LxThread *th = THREAD();
    ASSERT(IS_AN_ERROR(err));
    Value ret = newArray();
    setProp(err, internedString("backtrace", 9), ret);
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
            ObjString *outBuf = hiddenString("", 0);
            Value out = newStringInstance(outBuf);
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
            unhideFromGC((Obj*)outBuf);
        }
    }
    VM_DEBUG("/Setting backtrace");
}

static inline bool isThrowable(Value val) {
    return IS_AN_ERROR(val);
}

// FIXME: use v_includedMods
static bool lookupMethod(ObjInstance *obj, ObjClass *klass, ObjString *propName, Value *ret, bool lookInGivenClass) {
    ObjClass *givenClass = klass;
    if (klass == obj->klass && obj->singletonKlass) {
        klass = obj->singletonKlass;
    }
    Value key = OBJ_VAL(propName);
    while (klass) {
        if (!lookInGivenClass && klass == givenClass) {
            klass = CLASSINFO(klass)->superclass; // FIXME: work in modules
            continue;
        }
        if (tableGet(CLASSINFO(klass)->methods, key, ret)) {
            return true;
        }
        klass = CLASSINFO(klass)->superclass;
    }
    return false;
}

static InterpretResult vm_run(void);

static Value propertyGet(ObjInstance *obj, ObjString *propName) {
    Value ret;
    Obj *method = NULL;
    Obj *getter = NULL;
    if (tableGet(obj->fields, OBJ_VAL(propName), &ret)) {
        return ret;
    } else if ((getter = instanceFindGetter(obj, propName))) {
        VM_DEBUG("getter found");
        callVMMethod(obj, OBJ_VAL(getter), 0, NULL);
        if (THREAD()->hadError) {
            return NIL_VAL;
        } else {
            return pop();
        }
    } else if ((method = instanceFindMethod(obj, propName))) {
        ObjBoundMethod *bmethod = newBoundMethod(obj, method);
        return OBJ_VAL(bmethod);
    } else {
        return NIL_VAL;
    }
}

static void propertySet(ObjInstance *obj, ObjString *propName, Value rval) {
    if (isFrozen((Obj*)obj)) {
        throwErrorFmt(lxErrClass, "Tried to set property on frozen object");
    }
    Obj *setter = NULL;
    if ((setter = instanceFindSetter(obj, propName))) {
        VM_DEBUG("setter found");
        callVMMethod(obj, OBJ_VAL(setter), 1, &rval);
        pop();
    } else {
        tableSet(obj->fields, OBJ_VAL(propName), rval);
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
        VM_DEBUG("defining method '%s' in class '%s'", name->chars, klassName);
        tableSet(CLASSINFO(klass)->methods, OBJ_VAL(name), method);
    } else {
        ObjModule *mod = AS_MODULE(classOrMod);
        const char *modName = CLASSINFO(mod)->name ? CLASSINFO(mod)->name->chars : "(anon)";
        (void)modName;
        VM_DEBUG("defining method '%s' in module '%s'", name->chars, modName);
        tableSet(CLASSINFO(mod)->methods, OBJ_VAL(name), method);
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
    VM_DEBUG("defining static method '%s#%s'", CLASSINFO(metaClass)->name->chars, name->chars);
    tableSet(CLASSINFO(metaClass)->methods, OBJ_VAL(name), method);
    pop(); // function
}

static void defineGetter(ObjString *name) {
    Value method = peek(0); // function
    ASSERT(IS_CLOSURE(method));
    Value classOrMod = peek(1);
    ASSERT(IS_CLASS(classOrMod) || IS_MODULE(classOrMod));
    if (IS_CLASS(classOrMod)) {
        ObjClass *klass = AS_CLASS(classOrMod);
        VM_DEBUG("defining getter '%s'", name->chars);
        tableSet(CLASSINFO(klass)->getters, OBJ_VAL(name), method);
    } else {
        ObjModule *mod = AS_MODULE(classOrMod);
        VM_DEBUG("defining getter '%s'", name->chars);
        tableSet(CLASSINFO(mod)->getters, OBJ_VAL(name), method);
    }
    pop(); // function
}

static void defineSetter(ObjString *name) {
    Value method = peek(0); // function
    ASSERT(IS_CLOSURE(method));
    Value classOrMod = peek(1);
    if (IS_CLASS(classOrMod)) {
        ObjClass *klass = AS_CLASS(classOrMod);
        VM_DEBUG("defining setter '%s'", name->chars);
        tableSet(CLASSINFO(klass)->setters, OBJ_VAL(name), method);
    } else {
        ObjModule *mod = AS_MODULE(classOrMod);
        VM_DEBUG("defining setter '%s'", name->chars);
        tableSet(CLASSINFO(mod)->setters, OBJ_VAL(name), method);
    }
    pop(); // function
}

// Call method on instance, args are NOT expected to be pushed on to stack by
// caller, nor is the instance. `argCount` does not include the implicit instance argument.
// Return value is pushed to stack and returned.
Value callVMMethod(ObjInstance *instance, Value callable, int argCount, Value *args) {
    VM_DEBUG("Calling VM method");
    push(OBJ_VAL(instance));
    Obj *oldThis = THREAD()->thisObj;
    setThis(0);
    for (int i = 0; i < argCount; i++) {
        ASSERT(args);
        push(args[i]);
    }
    VM_DEBUG("call begin");
    callCallable(callable, argCount, true, NULL); // pushes return value to stack
    THREAD()->thisObj = oldThis;
    VM_DEBUG("call end");
    return peek(0);
}

Value callMethod(Obj *obj, ObjString *methodName, int argCount, Value *args) {
    if (obj->type == OBJ_T_INSTANCE) {
        ObjInstance *instance = (ObjInstance*)obj;
        Obj *callable = instanceFindMethod(instance, methodName);
        if (!callable && argCount == 0) {
            callable = instanceFindGetter(instance, methodName);
        }
        if (!callable) {
            ObjString *className = CLASSINFO(instance->klass)->name;
            const char *classStr = className->chars ? className->chars : "(anon)";
            throwErrorFmt(lxErrClass, "instance method '%s#%s' not found", classStr, methodName->chars);
        }
        callVMMethod(instance, OBJ_VAL(callable), argCount, args);
        return pop();
    } else if (obj->type == OBJ_T_CLASS) {
        ObjClass *klass = (ObjClass*)obj;
        Obj *callable = classFindStaticMethod(klass, methodName);
        /*if (!callable && numArgs == 0) {*/
        /*callable = instanceFindGetter((ObjInstance*)klass, mname);*/
        /*}*/
        if (!callable) {
            ObjString *className = CLASSINFO(klass)->name;
            const char *classStr = className ? className->chars : "(anon)";
            throwErrorFmt(lxErrClass, "class method '%s.%s' not found", classStr, methodName->chars);
        }
        callVMMethod((ObjInstance*)klass, OBJ_VAL(callable), argCount, args);
        return pop();
    } else if (obj->type == OBJ_T_MODULE) {
        ObjModule *mod = (ObjModule*)obj;
        Obj *callable = moduleFindStaticMethod(mod, methodName);
        if (!callable) {
            ObjString *modName = CLASSINFO(mod)->name;
            const char *modStr = modName ? modName->chars : "(anon)";
            throwErrorFmt(lxErrClass, "module method '%s.%s' not found", modStr, methodName->chars);
        }
        callVMMethod((ObjInstance*)mod, OBJ_VAL(callable), argCount, args);
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
    ErrTagInfo *info = THREAD()->errInfo;
    while (info && info->frame == frame) {
        ErrTagInfo *prev = info->prev;
        FREE(ErrTagInfo, info);
        info = prev;
    }
    THREAD()->errInfo = info;
}


void popFrame(void) {
    DBG_ASSERT(vm.inited);
    LxThread *th = THREAD();
    ASSERT(EC->frameCount >= 1);
    VM_DEBUG("popping callframe (%s)", getFrame()->isCCall ? "native" : "non-native");
    CallFrame *frame = getFrame();
    unwindErrInfo(frame);
    if (frame->isCCall) {
        if (th->inCCall > 0) {
            ASSERT(th->inCCall > 0);
            th->inCCall--;
            if (th->inCCall == 0) {
                vec_clear(&THREAD()->stackObjects);
            }
        }
    }
    memset(frame, 0, sizeof(*frame));
    EC->frameCount--;
    frame = getFrameOrNull();
    if (frame && frame->instance) {
        th->thisObj = (Obj*)frame->instance;
    } else {
        th->thisObj = NULL;
    }
    ASSERT_VALID_STACK();
}

CallFrame *pushFrame(void) {
    DBG_ASSERT(vm.inited);
    if (EC->frameCount >= FRAMES_MAX) {
        throwErrorFmt(lxErrClass, "Stackoverflow, max number of call frames (%d)", FRAMES_MAX);
        UNREACHABLE_RETURN(NULL);
    }
    CallFrame *frame = &EC->frames[EC->frameCount++];
    memset(frame, 0, sizeof(*frame));
    frame->callLine = curLine;
    /*Value curFile;*/
    ASSERT(vm.fileString);
    frame->file = EC->filename;
    return frame;
}

const char *callFrameName(CallFrame *frame) {
    DBG_ASSERT(frame);
    ObjString *fnName = frame->closure->function->name;
    return fnName ? fnName->chars : "<main>";
}

static void pushNativeFrame(ObjNative *native) {
    DBG_ASSERT(vm.inited);
    ASSERT(native);
    VM_DEBUG("Pushing native callframe for %s", native->name->chars);
    if (EC->frameCount == FRAMES_MAX) {
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
    THREAD()->inCCall++;
}

// sets up VM/C call jumpbuf if not set
static void captureNativeError(void) {
    LxThread *th = THREAD();
    if (th->inCCall == 0) {
        VM_DEBUG("%s", "Setting VM/C error jump buffer");
        VM_DEBUG("setting VM/C error jump buf\n");
        int jumpRes = setjmp(th->cCallJumpBuf);
        if (jumpRes == JUMP_SET) { // jump is set, prepared to enter C land
            th->cCallJumpBufSet = true;
            return;
        } else { // C call longjmped here from throwError()
            th = THREAD();
            ASSERT(th->inCCall > 0);
            ASSERT(th->cCallThrew);
            th->cCallThrew = false;
            th->returnedFromNativeErr = true;
            th->cCallJumpBufSet = false;
        }
    }
}

static bool checkFunctionArity(ObjFunction *func, int argCount) {
    int arityMin = func->arity;
    int arityMax = arityMin + func->numDefaultArgs + func->numKwargs;
    if (func->hasRestArg) arityMax = 20; // TODO: make a #define
    if (argCount < arityMin || argCount > arityMax) {
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
    LxThread *th = THREAD();
    ObjClosure *closure = NULL;
    Value instanceVal;
    ObjInstance *instance = NULL;
    ObjClass *frameClass = NULL;
    if (isMethod) {
        instance = AS_INSTANCE(EC->stackTop[-argCount-1]);
        if (!isInstanceLikeObj((Obj*)instance)) {
            throwErrorFmt(lxTypeErrClass, "Tried to invoke method on non-instance (type=%s)", typeOfObj((Obj*)instance));
        }
        frameClass = instance->klass; // TODO: make class the callable's class, not the instance class
    } else {
        // wrong usage of callCallable, callable should be on stack, below the arguments
        ASSERT(isCallable(EC->stackTop[-argCount-1])); // should be same as `callable`
    }
    if (IS_CLOSURE(callable)) { // lox function
        closure = AS_CLOSURE(callable);
        if (!isMethod) {
            EC->stackTop[-argCount - 1] = callable; // should already be the callable, but just in case
        }
    } else if (IS_CLASS(callable)) { // initializer
        ObjClass *klass = AS_CLASS(callable);
        const char *klassName = CLASSINFO(klass)->name ? CLASSINFO(klass)->name->chars : "(anon)";
        (void)klassName;
        VM_DEBUG("calling callable class %s", klassName);
        instance = newInstance(klass); // setup the new instance object
        frameClass = klass;
        instanceVal = OBJ_VAL(instance);
        Obj *oldThis = th->thisObj;
        th->thisObj = (Obj*)instance;
        /*ASSERT(IS_CLASS(EC->stackTop[-argCount - 1])); this holds true if the # of args is correct for the function */
        EC->stackTop[-argCount - 1] = instanceVal; // first argument is instance, replaces class object
        // Call the initializer, if there is one.
        Value initializer;
        Obj *init = instanceFindMethod(instance, vm.initString);
        isMethod = true;
        if (init) {
            VM_DEBUG("callable is initializer for class %s", klassName);
            initializer = OBJ_VAL(init);
            if (IS_NATIVE_FUNCTION(initializer)) {
                captureNativeError();
                VM_DEBUG("calling native initializer for class %s with %d args", klassName, argCount);
                ObjNative *nativeInit = AS_NATIVE_FUNCTION(initializer);
                ASSERT(nativeInit->function);
                pushNativeFrame(nativeInit);
                CallFrame *newFrame = getFrame();
                DBG_ASSERT(instance);
                newFrame->instance = instance;
                newFrame->klass = frameClass;
                nativeInit->function(argCount+1, EC->stackTop-argCount-1);
                th->thisObj = oldThis;
                newFrame->slots = EC->stackTop-argCount-1;
                if (th->returnedFromNativeErr) {
                    th->returnedFromNativeErr = false;
                    VM_DEBUG("native initializer returned from error");
                    vec_clear(&THREAD()->stackObjects);
                    while (getFrame() >= newFrame) {
                        popFrame();
                    }
                    ASSERT(th->inCCall == 0);
                    throwError(th->lastErrorThrown); // re-throw inside VM
                    return false;
                } else {
                    VM_DEBUG("native initializer returned");
                    EC->stackTop = getFrame()->slots;
                    popFrame();
                    push(OBJ_VAL(instance));
                    return true;
                }
            }
            VM_DEBUG("calling non-native initializer with %d args", argCount);
            ASSERT(IS_CLOSURE(initializer));
            closure = AS_CLOSURE(initializer);
        } else {
            throwArgErrorFmt("init() method not found?", argCount);
        }
    } else if (IS_BOUND_METHOD(callable)) {
        VM_DEBUG("calling bound method with %d args", argCount);
        ObjBoundMethod *bmethod = AS_BOUND_METHOD(callable);
        Obj *callable = bmethod->callable; // native function or user-defined function (ObjClosure)
        instanceVal = bmethod->receiver;
        EC->stackTop[-argCount - 1] = instanceVal;
        return doCallCallable(OBJ_VAL(callable), argCount, true, callInfo);
    } else if (IS_NATIVE_FUNCTION(callable)) {
#ifndef NDEBUG
        static const char callableNameBuf[200];
        fillCallableName(callable, callableNameBuf, 200);
        VM_DEBUG("Calling native %s %s with %d args", isMethod ? "method" : "function", callableNameBuf, argCount);
#endif
        ObjNative *native = AS_NATIVE_FUNCTION(callable);
        int argCountActual = argCount; // includes the callable on the stack, or the receiver if it's a method
        if (isMethod) {
            argCount++;
            argCountActual++;
            if (!instance) {
                instance = AS_INSTANCE(*(EC->stackTop-argCount));
                DBG_ASSERT(((Obj*)instance)->type == OBJ_T_INSTANCE);
                frameClass = instance->klass;
            }
        } else {
            argCountActual++;
        }
        captureNativeError();
        pushNativeFrame(native);
        CallFrame *newFrame = getFrame();
        newFrame->instance = instance;
        newFrame->klass = frameClass;
        Value val = native->function(argCount, EC->stackTop-argCount);
        newFrame->slots = EC->stackTop-argCountActual;
        if (th->returnedFromNativeErr) {
            VM_DEBUG("Returned from native function with error");
            th->returnedFromNativeErr = false;
            while (getFrame() >= newFrame) {
                popFrame();
            }
            ASSERT(th->inCCall == 0);
            throwError(th->lastErrorThrown); // re-throw inside VM
            return false;
        } else {
            VM_DEBUG("Returned from native function without error");
            EC->stackTop = getFrame()->slots;
            popFrame();
            push(val);
        }
        return true;
    } else {
        UNREACHABLE("bad callable value given to callCallable: %s", typeOfVal(callable));
    }

    if (EC->frameCount >= FRAMES_MAX) {
        errorPrintScriptBacktrace("Stack overflow.");
        return false;
    }

    VM_DEBUG("doCallCallable found closure");
    // non-native function/method (defined in lox code)
    ASSERT(closure);
    ObjFunction *func = closure->function;
    checkFunctionArity(func, argCount);

    vec_nodep_t *params = (vec_nodep_t*)nodeGetData(func->funcNode);
    ASSERT(params);

    Value kwargsMap = NIL_VAL;
    // keyword arg processing
    if (func->numKwargs > 0 && callInfo) {
        kwargsMap = newMap();
        Node *param = NULL;
        int pi = 0;
        vec_foreach_rev(params, param, pi) {
            if (param->type.kind == PARAM_NODE_KWARG) {
                char *kwname = tokStr(&param->tok);
                ObjString *kwStr = copyString(kwname, strlen(kwname));
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
                ObjString *kwStr = copyString(kwname, strlen(kwname));
                Value val;
                if (MAP_GET(kwargsMap, OBJ_VAL(kwStr), &val)) {
                    push(val);
                } else {
                    push(UNDEF_VAL);
                    numKwargsNotGiven++;
                }
            }
        }
        push(kwargsMap);
    }

    int parentStart = getFrame()->ip - getFrame()->closure->function->chunk->code - 2;
    ASSERT(parentStart >= 0);

    size_t funcOffset = 0;
    VM_DEBUG(
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
            if (param->type.kind == PARAM_NODE_DEFAULT_ARG) {
                size_t offset = ((ParamNodeInfo*)param->data)->defaultArgIPOffset;
                VM_DEBUG("default param found: offset=%d", (int)offset);
                funcOffset += offset;
                unused--;
                if (unused == 0) break;
            } else {
                ASSERT(0); // unreachable, default args should be last args, not including splats
                break;
            }
        }
    }

    // add frame
    VM_DEBUG("%s", "Pushing callframe (non-native)");
    CallFrame *frame = pushFrame();
    frame->instance = instance;
    if (instance && !frameClass) frameClass = instance->klass;
    frame->klass = frameClass;
    if (funcOffset > 0) {
        VM_DEBUG("Func offset due to optargs: %d", (int)funcOffset);
    }
    frame->closure = closure;
    frame->ip = closure->function->chunk->code + funcOffset;
    frame->start = parentStart;
    frame->isCCall = false;
    frame->nativeFunc = NULL;
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
    LxThread *th = THREAD();
    int lenBefore = th->stackObjects.length;
    bool ret = doCallCallable(callable, argCount, isMethod, info);
    int lenAfter = th->stackObjects.length;

    // allow collection of new stack-created objects if they're not rooted now
    for (int i = lenBefore; i < lenAfter; i++) {
        (void)vec_pop(&th->stackObjects);
    }

    return ret;
}

static Obj *findMethod(ObjClass *klass, ObjString *methodName) {
    Value method;
    while (klass) {
        if (tableGet(CLASSINFO(klass)->methods, OBJ_VAL(methodName), &method)) {
            return AS_OBJ(method);
        }
        klass = CLASSINFO(klass)->superclass;
    }
    return NULL;
}

// API for calling 'super' in native C methods
Value callSuper(int argCount, Value *args, CallInfo *cinfo) {
    if (!isClassHierarchyCreated) return NIL_VAL;
    (void)cinfo; // TODO: use
    CallFrame *frame = getFrame();
    DBG_ASSERT(frame->instance);
    DBG_ASSERT(frame->klass);
    if (!frame->isCCall) {
        throwErrorFmt(lxErrClass, "callSuper must be called from native C function!");
    }
    ObjNative *method = frame->nativeFunc;
    ASSERT(method);
    Obj *klass = method->klass;
    if (!klass) {
        throwErrorFmt(lxErrClass, "No class found for callSuper, current frame must be a method!");
    }
    ObjString *methodName = method->name;
    ASSERT(methodName);
    if (klass->type == OBJ_T_MODULE) {
        // TODO
    } else if (klass->type == OBJ_T_CLASS) {
        if ((ObjClass*)klass == lxObjClass) {
            return NIL_VAL;
        }
        ObjClass *superClass = CLASSINFO(klass)->superclass; // TODO: look in modules too
        if (!superClass) {
            throwErrorFmt(lxErrClass, "No superclass found for callSuper");
        }
        Obj *superMethod = findMethod(superClass, methodName);
        if (!superMethod) {
            throwErrorFmt(lxErrClass, "No super method found for callSuper");
        }
        LxThread *th = THREAD();
        int inCCall = th->inCCall;
        callVMMethod(frame->instance, OBJ_VAL(superMethod), argCount, args);
        ASSERT(th->inCCall == inCCall);
        return pop();
    } else {
        UNREACHABLE("bug");
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
    VM_DEBUG("findthrowjumploc");
    while (row || EC->frameCount >= 1) {
        VM_DEBUG("framecount: %d, num ECs: %d", EC->frameCount, th->v_ecs.length);
        if (row == NULL) { // pop a call frame
            VM_DEBUG("row null");
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
                ASSERT(EC->stackTop > getFrame()->slots);
                EC->stackTop = getFrame()->slots;
                popFrame();
                VM_DEBUG("frame popped");
                row = currentChunk()->catchTbl;
                continue;
            }
        }
        Value klassFound;
        if (!tableGet(&vm.globals, row->catchVal, &klassFound)) {
            VM_DEBUG("a class not found for row, next row");
            row = row->next;
            continue;
        }
        VM_DEBUG("a class found for row");
        if (IS_SUBCLASS(klass, AS_CLASS(klassFound))) {
            VM_DEBUG("good class found for row");
            if (poppedEC || (currentIpOff > row->ifrom && currentIpOff <= row->ito)) {
                // found target catch
                *ipOut = currentChunk()->code + row->itarget;
                *rowFound = row;
                VM_DEBUG("Catch jump location found");
                return true;
            }
        }
        row = row->next;
    }

    VM_DEBUG("Catch jump location NOT found");
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
    ErrTagInfo *cur = THREAD()->errInfo;
    while (cur) {
        // tag for all errors
        if (cur->errClass == NULL || cur->errClass == klass) {
            return cur;
        }
        cur = cur->prev;
    }
    return NULL;
}

NORETURN void throwError(Value self) {
    VM_DEBUG("throwing error");
    ASSERT(vm.inited);
    ASSERT(IS_INSTANCE(self));
    LxThread *th = THREAD();
    th->lastErrorThrown = self;
    if (IS_NIL(getProp(self, internedString("backtrace", 9)))) {
        setBacktrace(self);
    }
    // error from VM
    ObjInstance *obj = AS_INSTANCE(self);
    ObjClass *klass = obj->klass;
    CatchTable *catchRow;
    uint8_t *ipNew = NULL;
    ErrTagInfo *errInfo = NULL;
    if ((errInfo = findErrTag(klass))) {
        VM_DEBUG("longjmping to tag");
        longjmp(errInfo->jmpBuf, JUMP_PERFORMED);
    }
    if (th->inCCall > 0 && th->cCallJumpBufSet) {
        ASSERT(getFrame()->isCCall);
        VM_DEBUG("throwing error from C call, longjmping");
        ASSERT(!th->cCallThrew);
        th->cCallThrew = true;
        longjmp(th->cCallJumpBuf, JUMP_PERFORMED);
    }
    th->inCCall = 0;
    if (findThrowJumpLoc(klass, &ipNew, &catchRow)) {
        ASSERT(ipNew);
        ASSERT(catchRow);
        ASSERT(getFrame());
        catchRow->lastThrownValue = self;
        getFrame()->ip = ipNew;
        ASSERT(getFrame()->jmpBufSet);
        longjmp(getFrame()->jmpBuf, JUMP_PERFORMED);
    } else {
        ASSERT(rootVMLoopJumpBufSet);
        longjmp(rootVMLoopJumpBuf, JUMP_PERFORMED);
    }
    UNREACHABLE("after longjmp");
}

void popErrInfo(void) {
    THREAD()->errInfo = THREAD()->errInfo->prev;
}

void unsetErrInfo(void) {
    LxThread *th = THREAD();
    th->lastErrorThrown = NIL_VAL;
    ASSERT(th->errInfo);
    th->errInfo = th->errInfo->prev;
}

void rethrowErrInfo(ErrTagInfo *info) {
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
    ObjString *buf = takeString(cbuf, len);
    hideFromGC((Obj*)buf);
    Value msg = newStringInstance(buf);
    Value err = newError(klass, msg);
    THREAD()->lastErrorThrown = err;
    unhideFromGC((Obj*)buf);
    throwError(err);
    UNREACHABLE("thrown");
}

void printVMStack(FILE *f) {
    if (EC->stackTop == EC->stack && THREAD()->v_ecs.length == 1) {
        fprintf(f, "[DEBUG %d]: Stack: empty\n", THREAD()->vmRunLvl);
        return;
    }
    VMExecContext *ec = NULL; int i = 0;
    int numCallFrames = VMNumCallFrames();
    int numStackFrames = VMNumStackFrames();
    fprintf(f, "[DEBUG %d]: Stack (%d stack frames, %d call frames):\n", THREAD()->vmRunLvl,
            numStackFrames, numCallFrames);
    // print VM stack values from bottom of stack to top
    fprintf(f, "[DEBUG %d]: ", THREAD()->vmRunLvl);
    int callFrameIdx = 0;
    vec_foreach(&THREAD()->v_ecs, ec, i) {
        for (Value *slot = ec->stack; slot < ec->stackTop; slot++) {
            if (IS_OBJ(*slot) && (AS_OBJ(*slot)->type <= OBJ_T_NONE ||
                                 (AS_OBJ(*slot)->type >= OBJ_T_LAST))) {
                fprintf(stderr, "[DEBUG %d]: Broken object pointer: %p\n", THREAD()->vmRunLvl,
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
                if (objPtr->noGC) {
                    fprintf(f, " (hidden!)");
                }
            }
        }
    }
    fprintf(f, "\n");
}

ObjUpvalue *captureUpvalue(Value *local) {
    if (vm.openUpvalues == NULL) {
        vm.openUpvalues = newUpvalue(local);
        return vm.openUpvalues;
    }

    if (CLOX_OPTION_T(debugVM)) {
        VM_DEBUG("Capturing upvalue: ");
        printValue(stderr, *local, false, -1);
        fprintf(stderr, "\n");
    }

    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;

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
    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        // The new one is the first one in the list.
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(Value *last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->value >= last) {
    ObjUpvalue *upvalue = vm.openUpvalues;

    // Move the value into the upvalue itself and point the upvalue to it.
    upvalue->closed = *upvalue->value;
    upvalue->value = &upvalue->closed;

    // Pop it off the open upvalue list.
    vm.openUpvalues = upvalue->next;
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
        return internedString("opAdd", 5);
    case OP_SUBTRACT:
        return internedString("opDiff", 6);
    case OP_MULTIPLY:
        return internedString("opMul", 5);
    case OP_DIVIDE:
        return internedString("opDiv", 5);
    case OP_SHOVEL_L:
        return internedString("opShovelLeft", 12);
    case OP_SHOVEL_R:
        return internedString("opShovelRight", 13);
    default:
        return NULL;
    }
}

/**
 * Run the VM's instructions.
 */
static InterpretResult vm_run() {
    LxThread *th = THREAD();
    if (CLOX_OPTION_T(parseOnly) || CLOX_OPTION_T(compileOnly)) {
        return INTERPRET_OK;
    }

    if (!rootVMLoopJumpBufSet) {
        ASSERT(th->vmRunLvl == 0);
        int jumpRes = setjmp(rootVMLoopJumpBuf);
        rootVMLoopJumpBufSet = true;
        if (jumpRes == JUMP_SET) {
            VM_DEBUG("VM set rootVMLoopJumpBuf");
        } else {
            VM_DEBUG("VM caught error in rootVMLoopJumpBuf");
            showUncaughtError(THREAD()->lastErrorThrown);
            return INTERPRET_RUNTIME_ERROR;
        }
    }
    th->vmRunLvl++;
    Chunk *ch = currentChunk();
    if (ch->catchTbl != NULL) {
        CallFrame *f = getFrame();
        int jumpRes = setjmp(f->jmpBuf);
        if (jumpRes == JUMP_SET) {
            f->jmpBufSet = true;
            VM_DEBUG("VM set catch table for call frame (vm_run lvl %d)", th->vmRunLvl-1);
        } else {
            VM_DEBUG("VM caught error for call frame (vm_run lvl %d)", th->vmRunLvl-1);
            th->hadError = false;
            // stack is already unwound to proper frame
        }
    }
#define READ_BYTE() (*getFrame()->ip++)
#define READ_CONSTANT() (ch->constants->values[READ_BYTE()])
#define BINARY_OP(op, opcode, type) \
    do { \
      Value b = pop(); \
      Value a = pop(); \
      if (IS_NUMBER(a) && IS_NUMBER(b)) {\
          if ((opcode == OP_DIVIDE || opcode == OP_MODULO) && AS_NUMBER(b) == 0.00) {\
              throwErrorFmt(lxErrClass, "Can't divide by 0");\
          }\
          push(NUMBER_VAL((type)AS_NUMBER(a) op (type)AS_NUMBER(b))); \
      } else if (IS_INSTANCE(a)) {\
          push(a);\
          push(b);\
          ObjInstance *inst = AS_INSTANCE(a);\
          ObjString *methodName = methodNameForBinop(opcode);\
          Obj *callable = NULL;\
          if (methodName) {\
            callable = instanceFindMethod(inst, methodName);\
          }\
          if (!callable) {\
              throwErrorFmt(lxNameErrClass, "method %s not found for operation '%s'", methodName->chars, #op);\
          }\
          callCallable(OBJ_VAL(callable), 1, true, NULL);\
      } else {\
          throwErrorFmt(lxTypeErrClass, "binary operation type error, op=%s, lhs=%s, rhs=%s", #op, typeOfVal(a), typeOfVal(b));\
      }\
    } while (0)

  /*fprintf(stderr, "VM run level: %d\n", vmRunLvl);*/
  for (;;) {
      if (th->hadError) {
          (th->vmRunLvl)--;
          return INTERPRET_RUNTIME_ERROR;
      }
      if (vm.exited) {
          (th->vmRunLvl)--;
          return INTERPRET_OK;
      }
      if (EC->stackTop < EC->stack) {
          ASSERT(0);
      }

      int byteCount = (int)(getFrame()->ip - ch->code);
      curLine = ch->lines[byteCount];
      int lastLine = -1;
      int ndepth = ch->ndepths[byteCount];
      int nwidth = ch->nwidths[byteCount];
      /*fprintf(stderr, "line: %d, depth: %d, width: %d\n", curLine, ndepth, nwidth);*/
      if (byteCount > 0) {
          lastLine = ch->lines[byteCount-1];
      }
      if (shouldEnterDebugger(&vm.debugger, "", curLine, lastLine, ndepth, nwidth)) {
          enterDebugger(&vm.debugger, "", curLine, ndepth, nwidth);
      }

#ifndef NDEBUG
    if (CLOX_OPTION_T(traceVMExecution)) {
        printVMStack(stderr);
        printDisassembledInstruction(stderr, ch, (int)(getFrame()->ip - ch->code), NULL);
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

    uint8_t instruction = READ_BYTE();
    switch (instruction) {
      case OP_CONSTANT: { // numbers, code chunks
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      case OP_ADD:      BINARY_OP(+,OP_ADD, double); break;
      case OP_SUBTRACT: BINARY_OP(-,OP_SUBTRACT, double); break;
      case OP_MULTIPLY: BINARY_OP(*,OP_MULTIPLY, double); break;
      case OP_DIVIDE:   BINARY_OP(/,OP_DIVIDE, double); break;
      case OP_MODULO:   BINARY_OP(%,OP_MODULO, int); break;
      case OP_BITOR:    BINARY_OP(|,OP_BITOR, int); break;
      case OP_BITAND:   BINARY_OP(&,OP_BITAND, int); break;
      case OP_BITXOR:   BINARY_OP(^,OP_BITXOR, int); break;
      case OP_SHOVEL_L: BINARY_OP(<<,OP_SHOVEL_L,int); break;
      case OP_SHOVEL_R: BINARY_OP(>>,OP_SHOVEL_R,int); break;
      case OP_NEGATE: {
        Value val = pop();
        if (!IS_NUMBER(val)) {
            throwErrorFmt(lxTypeErrClass, "Can only negate numbers, type=%s", typeOfVal(val));
        }
        push(NUMBER_VAL(-AS_NUMBER(val)));
        break;
      }
      case OP_LESS: {
        Value rhs = pop(); // rhs
        Value lhs = pop(); // lhs
        if (!canCmpValues(lhs, rhs, instruction)) {
            throwErrorFmt(lxTypeErrClass,
                "Can only compare 2 numbers or 2 strings with '<', lhs=%s, rhs=%s",
                typeOfVal(lhs), typeOfVal(rhs));
            break;
        }
        if (cmpValues(lhs, rhs, instruction) == -1) {
            push(trueValue());
        } else {
            push(falseValue());
        }
        break;
      }
      case OP_GREATER: {
        Value rhs = pop();
        Value lhs = pop();
        if (!canCmpValues(lhs, rhs, instruction)) {
            throwErrorFmt(lxTypeErrClass,
                "Can only compare 2 numbers or 2 strings with '>', lhs=%s, rhs=%s",
                typeOfVal(lhs), typeOfVal(rhs));
            break;
        }
        if (cmpValues(lhs, rhs, instruction) == 1) {
            push(trueValue());
        } else {
            push(falseValue());
        }
        break;
      }
      case OP_EQUAL: {
          Value rhs = pop();
          Value lhs = pop();
          if (isValueOpEqual(lhs, rhs)) {
              push(trueValue());
          } else {
              push(falseValue());
          }
          break;
      }
      case OP_NOT_EQUAL: {
          Value rhs = pop();
          Value lhs = pop();
          if (isValueOpEqual(lhs, rhs)) {
              push(falseValue());
          } else {
              push(trueValue());
          }
          break;
      }
      case OP_NOT: {
          Value val = pop();
          push(BOOL_VAL(!isTruthy(val)));
          break;
      }
      case OP_GREATER_EQUAL: {
          Value rhs = pop();
          Value lhs = pop();
          if (!canCmpValues(lhs, rhs, instruction)) {
              throwErrorFmt(lxTypeErrClass,
                  "Can only compare 2 numbers or 2 strings with '>=', lhs=%s, rhs=%s",
                   typeOfVal(lhs), typeOfVal(rhs));
              break;
          }
          if (cmpValues(lhs, rhs, instruction) != -1) {
              push(trueValue());
          } else {
              push(falseValue());
          }
          break;
      }
      case OP_LESS_EQUAL: {
          Value rhs = pop();
          Value lhs = pop();
          if (!canCmpValues(lhs, rhs, instruction)) {
              throwErrorFmt(lxTypeErrClass,
                  "Can only compare 2 numbers or 2 strings with '<=', lhs=%s, rhs=%s",
                   typeOfVal(lhs), typeOfVal(rhs));
              break;
          }
          if (cmpValues(lhs, rhs, instruction) != 1) {
              push(trueValue());
          } else {
              push(falseValue());
          }
          break;
      }
      case OP_PRINT: {
        Value val = pop();
        if (!vm.printBuf || vm.printToStdout) {
            printValue(stdout, val, true, -1);
            printf("\n");
            fflush(stdout);
        }
        if (vm.printBuf) {
            ObjString *out = valueToString(val, hiddenString);
            ASSERT(out);
            pushCString(vm.printBuf, out->chars, strlen(out->chars));
            pushCString(vm.printBuf, "\n", 1);
            unhideFromGC((Obj*)out);
        }
        break;
      }
      case OP_DEFINE_GLOBAL: {
          Value varName = READ_CONSTANT();
          char *name = AS_CSTRING(varName);
          if (isUnredefinableGlobal(name)) {
              pop();
              throwErrorFmt(lxNameErrClass, "Can't redeclare global variable '%s'", name);
          }
          Value val = peek(0);
          tableSet(&vm.globals, varName, val);
          pop();
          break;
      }
      case OP_GET_GLOBAL: {
        Value varName = READ_CONSTANT();
        Value val;
        if (tableGet(&EC->roGlobals, varName, &val)) {
            push(val);
        } else if (tableGet(&vm.globals, varName, &val)) {
            push(val);
        } else {
            throwErrorFmt(lxNameErrClass, "Undefined global variable '%s'.", AS_STRING(varName)->chars);
        }
        break;
      }
      case OP_SET_GLOBAL: {
        Value val = peek(0);
        Value varName = READ_CONSTANT();
        char *name = AS_CSTRING(varName);
        if (isUnredefinableGlobal(name)) {
            throwErrorFmt(lxNameErrClass, "Can't redefine global variable '%s'", name);
        }
        tableSet(&vm.globals, varName, val);
        break;
      }
      case OP_NIL: {
        push(nilValue());
        break;
      }
      case OP_TRUE: {
        push(BOOL_VAL(true));
        break;
      }
      case OP_FALSE: {
        push(BOOL_VAL(false));
        break;
      }
      case OP_AND: {
          Value rhs = pop();
          Value lhs = pop();
          (void)lhs;
          // NOTE: we only check truthiness of rhs because lhs is
          // short-circuited (a JUMP_IF_FALSE is output in the bytecode for
          // the lhs).
          push(isTruthy(rhs) ? rhs : BOOL_VAL(false));
          break;
      }
      case OP_OR: {
          Value rhs = pop();
          Value lhs = pop();
          push(isTruthy(lhs) || isTruthy(rhs) ? rhs : lhs);
          break;
      }
      case OP_POP: {
          pop();
          break;
      }
      case OP_SET_LOCAL: {
          uint8_t slot = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          ASSERT(slot >= 0);
          getFrame()->slots[slot] = peek(0); // locals are popped at end of scope by VM
          break;
      }
      case OP_UNPACK_SET_LOCAL: {
          uint8_t slot = READ_BYTE();
          uint8_t unpackIdx = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          ASSERT(slot >= 0);
          // make sure we don't clobber the unpack array with the setting of
          // this variable
          int peekIdx = 0;
          while (getFrame()->slots+slot > EC->stackTop-1) {
              push(NIL_VAL);
              peekIdx++;
          }
          getFrame()->slots[slot] = unpackValue(peek(peekIdx+unpackIdx), unpackIdx); // locals are popped at end of scope by VM
          break;
      }
      case OP_GET_LOCAL: {
          uint8_t slot = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          ASSERT(slot >= 0);
          push(getFrame()->slots[slot]);
          break;
      }
      case OP_GET_UPVALUE: {
          uint8_t slot = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          push(*getFrame()->closure->upvalues[slot]->value);
          break;
      }
      case OP_SET_UPVALUE: {
          uint8_t slot = READ_BYTE();
          uint8_t varName = READ_BYTE(); // for debugging
          (void)varName;
          *getFrame()->closure->upvalues[slot]->value = peek(0);
          break;
      }
      case OP_CLOSE_UPVALUE: {
          closeUpvalues(EC->stackTop - 1);
          pop(); // pop the variable from the stack frame
          break;
      }
      case OP_CLOSURE: {
          Value funcVal = READ_CONSTANT();
          ASSERT(IS_FUNCTION(funcVal));
          ObjFunction *func = AS_FUNCTION(funcVal);
          ObjClosure *closure = newClosure(func);
          push(OBJ_VAL(closure));

          // capture upvalues
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
          break;
      }
      case OP_JUMP_IF_FALSE: {
          Value cond = pop();
          uint8_t ipOffset = READ_BYTE();
          if (!isTruthy(cond)) {
              ASSERT(ipOffset > 0);
              getFrame()->ip += (ipOffset-1);
          }
          break;
      }
      case OP_JUMP_IF_TRUE: {
          Value cond = pop();
          uint8_t ipOffset = READ_BYTE();
          if (isTruthy(cond)) {
              ASSERT(ipOffset > 0);
              getFrame()->ip += (ipOffset-1);
          }
          break;
      }
      case OP_JUMP_IF_FALSE_PEEK: {
          Value cond = peek(0);
          uint8_t ipOffset = READ_BYTE();
          if (!isTruthy(cond)) {
              ASSERT(ipOffset > 0);
              getFrame()->ip += (ipOffset-1);
          }
          break;
      }
      case OP_JUMP_IF_TRUE_PEEK: {
          Value cond = peek(0);
          uint8_t ipOffset = READ_BYTE();
          if (isTruthy(cond)) {
              ASSERT(ipOffset > 0);
              getFrame()->ip += (ipOffset-1);
          }
          break;
      }
      case OP_JUMP: {
          uint8_t ipOffset = READ_BYTE();
          ASSERT(ipOffset > 0);
          getFrame()->ip += (ipOffset-1);
          break;
      }
      case OP_LOOP: {
          uint8_t ipOffset = READ_BYTE();
          ASSERT(ipOffset > 0);
          // add 1 for the instruction we just read, and 1 to go 1 before the
          // instruction we want to execute next.
          getFrame()->ip -= (ipOffset+2);
          break;
      }
      case OP_CALL: {
          uint8_t numArgs = READ_BYTE();
          if (th->lastSplatNumArgs > 0) {
              numArgs += (th->lastSplatNumArgs-1);
              th->lastSplatNumArgs = -1;
          }
          Value callableVal = peek(numArgs);
          if (!isCallable(callableVal)) {
              for (int i = 0; i < numArgs; i++) {
                  pop();
              }
              throwErrorFmt(lxTypeErrClass, "Tried to call uncallable object (type=%s)", typeOfVal(callableVal));
          }
          Value callInfoVal = READ_CONSTANT();
          CallInfo *callInfo = internalGetData(AS_INTERNAL(callInfoVal));
          // ex: String("hi"), "hi" already evaluates to a string instance, so we just
          // return that.
          if (numArgs == 1 && strcmp(tokStr(&callInfo->nameTok), "String") == 0 && IS_A_STRING(peek(0))) {
              Value strVal = pop();
              pop();
              push(strVal);
              break;
          }
          callCallable(callableVal, numArgs, false, callInfo);
          ASSERT_VALID_STACK();
          break;
      }
      case OP_CHECK_KEYWORD: {
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
        break;
      }
      case OP_INVOKE: { // invoke methods (includes static methods)
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
          if (IS_INSTANCE(instanceVal)) {
              ObjInstance *inst = AS_INSTANCE(instanceVal);
              Obj *callable = instanceFindMethod(inst, mname);
              if (!callable && numArgs == 0) {
                  callable = instanceFindGetter(inst, mname);
              }
              if (!callable) {
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
              if (!callable) {
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
              Obj *callable = moduleFindStaticMethod(mod, mname);
              /*if (!callable && numArgs == 0) {*/
                  /*callable = instanceFindGetter((ObjInstance*)mod, mname);*/
              /*}*/
              if (!callable) {
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
          break;
      }
      case OP_GET_THIS: {
          ASSERT(th->thisObj);
          push(OBJ_VAL(th->thisObj));
          break;
      }
      case OP_SPLAT_ARRAY: {
          Value ary = pop();
          if (!IS_AN_ARRAY(ary)) {
              throwErrorFmt(lxTypeErrClass, "Splatted expression must evaluate to an Array (type=%s)",
                      typeOfVal(ary));
          }
          th->lastSplatNumArgs = ARRAY_SIZE(ary);
          for (int i = 0; i < th->lastSplatNumArgs; i++) {
              push(ARRAY_GET(ary, i));
          }
          break;
      }
      case OP_GET_SUPER: {
          Value methodName = READ_CONSTANT();
          ASSERT(th->thisObj);
          Value instanceVal = OBJ_VAL(th->thisObj);
          ASSERT(IS_INSTANCE(instanceVal)); // FIXME: get working for classes (singleton methods)
          ObjClass *klass = (ObjClass*)getFrame()->closure->function->klass;
          ASSERT(klass); // TODO: get working for module functions that call super
          ASSERT(((Obj*) klass)->type == OBJ_T_CLASS);
          Value method;
          bool found = lookupMethod(
              AS_INSTANCE(instanceVal), klass,
              AS_STRING(methodName), &method, false);
          if (!found) {
              throwErrorFmt(lxErrClass, "Could not find method for 'super': %s",
                      AS_CSTRING(methodName));
          }
          ObjBoundMethod *bmethod = newBoundMethod(AS_INSTANCE(instanceVal), AS_OBJ(method));
          push(OBJ_VAL(bmethod));
          break;
      }
      // return from function/method
      case OP_RETURN: {
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
      case OP_ITER: {
          Value iterable = peek(0);
          if (!isIterableType(iterable)) {
              throwErrorFmt(lxTypeErrClass, "Non-iterable value given to 'foreach' statement. Type found: %s",
                      typeOfVal(iterable));
          }
          Value iterator = createIterator(iterable);
          DBG_ASSERT(isIterator(iterator));
          DBG_ASSERT(isIterableType(peek(0)));
          pop(); // iterable
          push(iterator);
          break;
      }
      case OP_ITER_NEXT: {
          Value iterator = peek(0);
          ASSERT(isIterator(iterator));
          Value next = iteratorNext(iterator);
          ASSERT(!IS_UNDEF(next));
          push(next);
          break;
      }
      case OP_CLASS: { // add or re-open class
          Value className = READ_CONSTANT();
          Value existingClass;
          // FIXME: not perfect, if class is declared non-globally this won't
          // detect it. Maybe a new op-code is needed for class re-opening.
          if (tableGet(&vm.globals, className, &existingClass)) {
              if (IS_CLASS(existingClass)) { // re-open class
                  push(existingClass);
                  break;
              } else if (IS_MODULE(existingClass)) {
                  const char *classStr = AS_CSTRING(className);
                  throwErrorFmt(lxTypeErrClass, "Tried to define class %s, but it's a module",
                          classStr);
              } // otherwise we override the global var with the new class
          }
          ObjClass *klass = newClass(AS_STRING(className), lxObjClass);
          push(OBJ_VAL(klass));
          setThis(0);
          break;
      }
      case OP_MODULE: { // add or re-open module
          Value modName = READ_CONSTANT();
          Value existingMod;
          // FIXME: not perfect, if class is declared non-globally this won't
          // detect it. Maybe a new op-code is needed for class re-opening.
          if (tableGet(&vm.globals, modName, &existingMod)) {
              if (IS_MODULE(existingMod)) {
                push(existingMod); // re-open the module
                break;
              } else if (IS_CLASS(existingMod)) {
                  const char *modStr = AS_CSTRING(modName);
                  throwErrorFmt(lxTypeErrClass, "Tried to define module %s, but it's a class",
                          modStr);
              } // otherwise, we override the global var with the new module
          }
          ObjModule *mod = newModule(AS_STRING(modName));
          push(OBJ_VAL(mod));
          setThis(0);
          break;
      }
      case OP_SUBCLASS: { // add new class inheriting from an existing class
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
              if (IS_CLASS(existingClass)) {
                  throwErrorFmt(lxNameErrClass, "Class %s already exists (if "
                          "re-opening class, no superclass should be given)",
                          AS_CSTRING(className));
              } else if (IS_MODULE(existingClass)) {
                  throwErrorFmt(lxTypeErrClass, "Tried to define class %s, but it's a module", AS_CSTRING(className));
              }
          }
          ObjClass *klass = newClass(
              AS_STRING(className),
              AS_CLASS(superclass)
          );
          push(OBJ_VAL(klass));
          setThis(0);
          break;
      }
      case OP_IN: {
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
          break;
      }
      case OP_METHOD: { // method definition in class or module
          Value methodName = READ_CONSTANT();
          ObjString *methStr = AS_STRING(methodName);
          defineMethod(methStr);
          break;
      }
      case OP_CLASS_METHOD: { // method definition
          Value methodName = READ_CONSTANT();
          ObjString *methStr = AS_STRING(methodName);
          defineStaticMethod(methStr);
          break;
      }
      case OP_GETTER: { // getter method definition
          Value methodName = READ_CONSTANT();
          ObjString *methStr = AS_STRING(methodName);
          defineGetter(methStr);
          break;
      }
      case OP_SETTER: { // setter method definition
          Value methodName = READ_CONSTANT();
          ObjString *methStr = AS_STRING(methodName);
          defineSetter(methStr);
          break;
      }
      case OP_PROP_GET: {
          Value propName = READ_CONSTANT();
          ObjString *propStr = AS_STRING(propName);
          ASSERT(propStr && propStr->chars);
          Value instance = peek(0);
          if (!IS_INSTANCE_LIKE(instance)) {
              pop();
              throwErrorFmt(lxTypeErrClass, "Tried to access property '%s' of non-instance (type: %s)", propStr->chars, typeOfVal(instance));
          }
          pop();
          push(propertyGet(AS_INSTANCE(instance), propStr));
          break;
      }
      case OP_PROP_SET: {
          Value propName = READ_CONSTANT();
          ObjString *propStr = AS_STRING(propName);
          Value rval = peek(0);
          Value instance = peek(1);
          if (!IS_INSTANCE_LIKE(instance)) {
              pop(); pop();
              throwErrorFmt(lxTypeErrClass, "Tried to set property '%s' of non-instance", propStr->chars);
          }
          propertySet(AS_INSTANCE(instance), propStr, rval); // TODO: check frozenness of object
          pop(); // leave rval on stack
          pop();
          push(rval);
          break;
      }
      case OP_INDEX_GET: {
          Value lval = peek(1); // ex: Array/String/instance object
          if (!IS_INSTANCE_LIKE(lval)) {
              throwErrorFmt(lxTypeErrClass, "Cannot call opIndexGet ('[]') on a non-instance, found a: %s", typeOfVal(lval));
          }
          ObjInstance *instance = AS_INSTANCE(lval);
          Obj *method = instanceFindMethodOrRaise(instance, internedString("opIndexGet", 10));
          callCallable(OBJ_VAL(method), 1, true, NULL);
          break;
      }
      case OP_INDEX_SET: {
          Value lval = peek(2);
          if (!IS_INSTANCE_LIKE(lval)) {
              throwErrorFmt(lxTypeErrClass, "Cannot call opIndexSet ('[]=') on a non-instance, found a: %s", typeOfVal(lval));
          }
          ObjInstance *instance = AS_INSTANCE(lval);
          Obj *method = instanceFindMethodOrRaise(instance, internedString("opIndexSet", 10));
          callCallable(OBJ_VAL(method), 2, true, NULL);
          break;
      }
      case OP_THROW: {
          Value throwable = pop();
          if (IS_A_STRING(throwable)) {
              Value msg = throwable;
              throwable = newError(lxErrClass, msg);
          }
          if (!isThrowable(throwable)) {
              throwErrorFmt(lxTypeErrClass, "Tried to throw unthrowable value, must be a subclass of Error. "
                  "Type found: %s", typeOfVal(throwable)
              );
          }
          throwError(throwable);
          UNREACHABLE("after throw");
      }
      case OP_GET_THROWN: {
          Value catchTblIdx = READ_CONSTANT();
          ASSERT(IS_NUMBER(catchTblIdx));
          double idx = AS_NUMBER(catchTblIdx);
          CatchTable *tblRow = getCatchTableRow((int)idx);
          if (!isThrowable(tblRow->lastThrownValue)) { // bug
              fprintf(stderr, "Non-throwable found (BUG): %s\n", typeOfVal(tblRow->lastThrownValue));
              ASSERT(0);
          }
          push(tblRow->lastThrownValue);
          break;
      }
      case OP_STRING: {
          Value strLit = READ_CONSTANT();
          ASSERT(IS_STRING(strLit));
          uint8_t isStatic = READ_BYTE();
          push(OBJ_VAL(lxStringClass));
          ObjString *buf = AS_STRING(strLit);
          if (isStatic) {
            buf->isStatic = true;
            push(OBJ_VAL(buf));
          } else {
            push(OBJ_VAL(dupString(buf)));
          }
          bool ret = callCallable(peek(1), 1, false, NULL);
          ASSERT(ret); // the string instance is pushed to top of stack
          if (isStatic == 1) {
              objFreeze(AS_OBJ(peek(0)));
          }
          break;
      }
      case OP_ARRAY: {
          uint8_t numEls = READ_BYTE();
          Value aryVal = newArray();
          ValueArray *ary = ARRAY_GETHIDDEN(aryVal);
          for (int i = 0; i < numEls; i++) {
              Value el = pop();
              writeValueArrayEnd(ary, el);
          }
          push(aryVal);
          break;
      }
      case OP_MAP: {
          uint8_t numKeyVals = READ_BYTE();
          DBG_ASSERT(numKeyVals % 2 == 0);
          Value mapVal = newMap();
          Table *map = MAP_GETHIDDEN(mapVal);
          for (int i = 0; i < numKeyVals; i+=2) {
              Value key = pop();
              Value val = pop();
              tableSet(map, key, val);
          }
          push(mapVal);
          break;
      }
      // exit interpreter, or evaluation context if in eval() or
      // loadScript/requireScript
      case OP_LEAVE: {
          if (!isInEval() && !isInLoadedScript()) {
              vm.exited = true;
          }
          (th->vmRunLvl)--;
          return INTERPRET_OK;
      }
      default:
          errorPrintScriptBacktrace("Unknown opcode instruction: %s (%d)", opName(instruction), instruction);
          (th->vmRunLvl)--;
          return INTERPRET_RUNTIME_ERROR;
    }
  }

  UNREACHABLE_RETURN(INTERPRET_RUNTIME_ERROR);
#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

static void setupPerScriptROGlobals(char *filename) {
    ObjString *file = copyString(filename, strlen(filename));
    Value fileString = newStringInstance(file);
    hideFromGC(AS_OBJ(fileString));
    // NOTE: this can trigger GC, so we hide the value first
    tableSet(&EC->roGlobals, OBJ_VAL(vm.fileString), fileString);
    unhideFromGC(AS_OBJ(fileString));

    if (filename[0] == pathSeparator) {
        char *lastSep = rindex(filename, pathSeparator);
        int len = lastSep - filename;
        ObjString *dir = copyString(filename, len);
        Value dirVal = newStringInstance(dir);
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
    EC->filename = copyString(filename, strlen(filename));
    EC->frameCount = 0;
    VM_DEBUG("%s", "Pushing initial callframe");
    CallFrame *frame = pushFrame();
    frame->start = 0;
    frame->ip = chunk->code;
    frame->slots = EC->stack;
    ObjFunction *func = newFunction(chunk, NULL);
    hideFromGC((Obj*)func);
    frame->closure = newClosure(func);
    frame->isCCall = false;
    frame->nativeFunc = NULL;
    setupPerScriptROGlobals(filename);

    InterpretResult result = vm_run();
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
    EC->filename = copyString(filename, strlen(filename));
    VM_DEBUG("%s", "Pushing initial callframe");
    CallFrame *frame = pushFrame();
    frame->start = 0;
    frame->ip = chunk->code;
    frame->slots = EC->stack;
    ObjFunction *func = newFunction(chunk, NULL);
    hideFromGC((Obj*)func);
    frame->closure = newClosure(func);
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
        VM_DEBUG("compile error in eval");
        pop_EC();
        ASSERT(getFrame() == oldFrame);
        if (throwOnErr) {
            throwErrorFmt(lxSyntaxErrClass, "%s", "Syntax error");
        } else {
            // TODO: output error messages
            return UNDEF_VAL;
        }
    }
    EC->filename = copyString(filename, strlen(filename));
    VM_DEBUG("%s", "Pushing initial eval callframe");
    CallFrame *frame = pushFrame();
    frame->start = 0;
    frame->ip = chunk->code;
    frame->slots = EC->stack;
    ObjFunction *func = newFunction(chunk, NULL);
    hideFromGC((Obj*)func);
    frame->closure = newClosure(func);
    unhideFromGC((Obj*)func);
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
    VM_DEBUG("eval finished: error: %d", THREAD()->hadError ? 1 : 0);
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

static void unwindJumpRecover(ErrTagInfo *info) {
    ASSERT(info);
    DBG_ASSERT(getFrame());
    LxThread *th = THREAD();
    while (getFrame() != info->frame) {
        VM_DEBUG("popping callframe from unwind");
        popFrame();
    }
    while (th->errInfo != info) {
        VM_DEBUG("freeing Errinfo");
        ASSERT(th->errInfo);
        ErrTagInfo *prev = th->errInfo->prev;
        ASSERT(prev);
        FREE(ErrTagInfo, th->errInfo);
        th->errInfo = prev;
    }
}

void *vm_protect(vm_cb_func func, void *arg, ObjClass *errClass, ErrTag *status) {
    LxThread *th = THREAD();
    addErrInfo(errClass);
    ErrTagInfo *errInfo = th->errInfo;
    int jmpres = 0;
    if ((jmpres = setjmp(errInfo->jmpBuf)) == JUMP_SET) {
        *status = TAG_NONE;
        VM_DEBUG("vm_protect before func");
        void *res = func(arg);
        ErrTagInfo *prev = errInfo->prev;
        FREE(ErrTagInfo, errInfo);
        th->errInfo = prev;
        VM_DEBUG("vm_protect after func");
        return res;
    } else if (jmpres == JUMP_PERFORMED) {
        VM_DEBUG("vm_protect got to longjmp");
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

ErrTagInfo *addErrInfo(ObjClass *errClass) {
    LxThread *th = THREAD();
    struct ErrTagInfo *info = ALLOCATE(ErrTagInfo, 1);
    info->status = TAG_NONE;
    info->errClass = errClass;
    info->frame = getFrame();
    info->prev = th->errInfo;
    th->errInfo = info;
    info->caughtError = NIL_VAL;
    return info;
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

NORETURN void stopVM(int status) {
    if (THREAD() == vm.mainThread) {
        runAtExitHooks();
        freeVM();
        if (GET_OPTION(profileGC)) {
            printGCProfile();
        }
        vm.exited = true;
        _exit(status);
    } else {
        _exit(status);
    }
}

void acquireGVL(void) {
    pthread_mutex_lock(&vm.GVLock);
    while (vm.GVLockStatus > 0) {
        pthread_cond_wait(&vm.GVCond, &vm.GVLock); // block on wait queue
    }
    vm.GVLockStatus = 1;
    pthread_mutex_unlock(&vm.GVLock);
}

void releaseGVL(void) {
    pthread_mutex_lock(&vm.GVLock);
    vm.GVLockStatus = 0;
    pthread_mutex_unlock(&vm.GVLock);
    pthread_cond_signal(&vm.GVCond); // signal waiters
}

LxThread *FIND_THREAD(pthread_t tid) {
    ObjInstance *threadInstance; int thIdx = 0;
    LxThread *th = NULL;
    vec_foreach(&vm.threads, threadInstance, thIdx) {
        th = THREAD_GETHIDDEN(OBJ_VAL(threadInstance));
        ASSERT(th);
        if (th->tid == tid) return th;
    }
    return NULL;
}

LxThread *FIND_NEW_THREAD(pthread_t tid) {
    ObjInstance *threadInstance; int thIdx = 0;
    LxThread *th = NULL;
    vec_foreach_rev(&vm.threads, threadInstance, thIdx) {
        th = THREAD_GETHIDDEN(OBJ_VAL(threadInstance));
        ASSERT(th);
        if (th->status == THREAD_READY) return th;
    }
    return NULL;
}

ObjInstance *FIND_THREAD_INSTANCE(pthread_t tid) {
    ObjInstance *threadInstance; int thIdx = 0;
    LxThread *th = NULL;
    vec_foreach(&vm.threads, threadInstance, thIdx) {
        th = THREAD_GETHIDDEN(OBJ_VAL(threadInstance));
        ASSERT(th);
        if (th->tid == tid) return threadInstance;
    }
    return NULL;
}

LxThread *THREAD() {
    if (!vm.curThread) {
        vm.curThread = FIND_THREAD(GVLOwner);
    }
    return vm.curThread;
}
