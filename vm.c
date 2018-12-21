#include <pthread.h>
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

#ifndef NDEBUG
#define VM_DEBUG(...) vm_debug(__VA_ARGS__)
#define VM_WARN(...) vm_warn(__VA_ARGS__)
#else
#define VM_DEBUG(...) (void)0
#define VM_WARN(...) (void)0
#endif

static int vmRunLvl = 0;

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
}

// Builtin classes initialized below
ObjClass *lxObjClass;
ObjClass *lxStringClass;
ObjClass *lxClassClass;
ObjClass *lxModuleClass;
ObjClass *lxAryClass;
ObjClass *lxIteratorClass;
ObjClass *lxThreadClass;
ObjModule *lxGCModule;
ObjClass *lxErrClass;
ObjClass *lxArgErrClass;
ObjClass *lxTypeErrClass;
ObjClass *lxNameErrClass;
ObjClass *lxSyntaxErrClass;
ObjClass *lxLoadErrClass;

Value lxLoadPath; // load path for loadScript/requireScript (-L flag)

static void defineNativeClasses(void) {
    // class Object
    ObjClass *objClass = addGlobalClass("Object", NULL);
    addNativeMethod(objClass, "dup", lxObjectDup);
    addNativeGetter(objClass, "_class", lxObjectGetClass);
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
    addNativeMethod(classClass, "init", lxClassInit);
    addNativeMethod(classClass, "include", lxClassInclude);
    addNativeGetter(classClass, "_superClass", lxClassGetSuperclass);
    addNativeGetter(classClass, "name", lxClassGetName);

    addNativeMethod(modClass, "init", lxModuleInit);

    Init_ArrayClass();

    Init_MapClass();

    // class Iterator
    ObjClass *iterClass = addGlobalClass("Iterator", objClass);
    lxIteratorClass = iterClass;
    addNativeMethod(iterClass, "init", lxIteratorInit);
    addNativeMethod(iterClass, "next", lxIteratorNext);

    // class Error
    ObjClass *errClass = addGlobalClass("Error", objClass);
    lxErrClass = errClass;
    addNativeMethod(errClass, "init", lxErrInit);

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

    // class Thread
    ObjClass *threadClass = addGlobalClass("Thread", objClass);
    lxThreadClass = threadClass;

    // module GC
    ObjModule *GCModule = addGlobalModule("GC");
    ObjClass *GCClassStatic = moduleSingletonClass(GCModule);
    addNativeMethod(GCClassStatic, "stats", lxGCStats);
    addNativeMethod(GCClassStatic, "collect", lxGCCollect);
    lxGCModule = GCModule;

    // order of initialization not important here
    Init_ProcessModule();
    Init_FileClass();
    Init_IOModule();
}

static void defineGlobalVariables(void) {
    lxLoadPath = newArray();
    ObjString *loadPathStr = internedString("loadPath", 8);
    tableSet(&vm.globals, OBJ_VAL(loadPathStr), lxLoadPath);
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
        Value iter = OBJ_VAL(iterObj);
        Value args[2];
        args[0] = iter;
        args[1] = iterable;
        lxIteratorInit(2, args);
        return iter;
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

// VM/C call "context switch" data
static jmp_buf CCallJumpBuf;
unsigned inCCall;
static bool cCallThrew = false;
static bool returnedFromNativeErr = false;

static int lastSplatNumArgs = -1; // FIXME: this is really ugly

// Uncaught errors jump here
static jmp_buf rootVMLoopJumpBuf;
static bool rootVMLoopJumpBufSet = false;

static int curLine = 1;

// Add and use a new execution context
static inline void push_EC(void) {
    VMExecContext *ectx = ALLOCATE(VMExecContext, 1);
    memset(ectx, 0, sizeof(*ectx));
    initTable(&ectx->roGlobals);
    vec_push(&vm.v_ecs, ectx);
    vm.ec = ectx;
}

// Pop the current execution context and use the one created before
// the current one.
static inline void pop_EC(void) {
    ASSERT(vm.v_ecs.length > 0);
    VMExecContext *ctx = (VMExecContext*)vec_pop(&vm.v_ecs);
    freeTable(&ctx->roGlobals);
    FREE(VMExecContext, ctx);
    vm.ec = (VMExecContext*)vec_last(&vm.v_ecs);
}

static inline bool isInEval(void) {
    return EC->evalContext;
}

static inline bool isInLoadedScript(void) {
    return EC->loadContext;
}

// reset (clear) value stack for current execution context
void resetStack(void) {
    EC->stackTop = EC->stack;
    EC->frameCount = 0;
}

#define FIRST_GC_THRESHHOLD (1024*1024)

static void initMainThread(void) {
    if (pthread_mutex_init(&vm.GVLock, NULL) != 0) {
        die("Global VM lock unable to initialize");
    }

    vm.curThread = NULL;
    vm.mainThread = NULL;

    Value mainThread = newThread();
    Value threadList = newArray();
    arrayPush(threadList, mainThread);

    vm.curThread = AS_INSTANCE(mainThread);
    vm.mainThread = AS_INSTANCE(mainThread);
    vm.threads = AS_INSTANCE(threadList);

    acquireGVL();
    threadSetStatus(mainThread, THREAD_RUNNING);
    pthread_t tid = pthread_self();
    threadSetId(mainThread, tid);
    THREAD_DEBUG(1, "Main thread initialized");
}

void initVM() {
    if (vm.inited) {
        VM_WARN("initVM: VM already initialized");
        return;
    }
    VM_DEBUG("initVM() start");
    turnGCOff();
    vec_init(&vm.v_ecs);
    push_EC();
    resetStack();
    vm.objects = NULL;

    vm.bytesAllocated = 0;
    vm.nextGCThreshhold = FIRST_GC_THRESHHOLD;
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;
    vm.openUpvalues = NULL;
    vm.printBuf = NULL;
    vec_init(&vm.loadedScripts);

    vm.lastValue = NULL;
    vm.thisValue = NULL;
    initTable(&vm.globals);
    initTable(&vm.strings); // interned strings
    vm.inited = true; // NOTE: VM has to be inited before creation of strings
    vm.exited = false;
    vm.initString = internedString("init", 4);
    vm.fileString = internedString("__FILE__", 8);
    vm.dirString = internedString("__DIR__", 7);
    defineNativeFunctions();
    defineNativeClasses();
    vec_init(&vm.hiddenObjs);
    vec_init(&vm.stackObjects);

    vec_init(&vm.exitHandlers);

    initDebugger(&vm.debugger);

    vm.lastErrorThrown = NIL_VAL;
    vm.hadError = false;
    vm.errInfo = NULL;
    inCCall = 0;
    cCallThrew = false;
    returnedFromNativeErr = false;
    curLine = 1;
    memset(&CCallJumpBuf, 0, sizeof(CCallJumpBuf));

    memset(&rootVMLoopJumpBuf, 0, sizeof(rootVMLoopJumpBuf));
    rootVMLoopJumpBufSet = false;

    defineGlobalVariables();
    initMainThread();
    resetStack();
    turnGCOn();
    vmRunLvl = 0;
    VM_DEBUG("initVM() end");
}

void freeVM(void) {
    /*fprintf(stderr, "VM run level: %d\n", vmRunLvl);*/
    if (!vm.inited) {
        VM_WARN("freeVM: VM not yet initialized");
        return;
    }
    VM_DEBUG("freeVM() start");
    vm.initString = NULL;
    vm.fileString = NULL;
    vm.dirString = NULL;
    vm.hadError = false;
    vm.printBuf = NULL;
    vm.printToStdout = true;
    vm.lastValue = NULL;
    vm.thisValue = NULL;
    vm.openUpvalues = NULL;
    vec_deinit(&vm.hiddenObjs);
    vec_deinit(&vm.loadedScripts);

    freeDebugger(&vm.debugger);

    inCCall = 0;
    cCallThrew = false;
    returnedFromNativeErr = false;
    memset(&CCallJumpBuf, 0, sizeof(CCallJumpBuf));
    curLine = 1;
    vm.errInfo = NULL;

    memset(&rootVMLoopJumpBuf, 0, sizeof(rootVMLoopJumpBuf));
    rootVMLoopJumpBufSet = false;

    vec_deinit(&vm.stackObjects);
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeObjects();
    vm.objects = NULL;

    vec_clear(&vm.v_ecs);
    vm.ec = NULL;
    vm.inited = false;
    vm.exited = false;

    vec_deinit(&vm.exitHandlers);

    vmRunLvl = 0;
    releaseGVL();
    pthread_mutex_destroy(&vm.GVLock);
    vm.curThread = NULL;
    vm.mainThread = NULL;
    vm.threads = NULL;

    VM_DEBUG("freeVM() end");
}


int VMNumStackFrames(void) {
    VMExecContext *firstEC = vec_first(&vm.v_ecs);
    return EC->stackTop - firstEC->stack;
}

int VMNumCallFrames(void) {
    int ret = 0;
    VMExecContext *ec; int i = 0;
    vec_foreach(&vm.v_ecs, ec, i) {
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
    vm.lastValue = EC->lastValue;
    return *vm.lastValue;
}

Value peek(unsigned n) {
    ASSERT((EC->stackTop-n) > EC->stack);
    return *(EC->stackTop-1-n);
}

static inline void setThis(unsigned n) {
    ASSERT((EC->stackTop-n) > EC->stack);
    vm.thisValue = (EC->stackTop-1-n);
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

    // TODO: error out
    return -2;
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

static inline CallFrame *getFrameOrNull(void) {
    if (EC->frameCount == 0) {
        return NULL;
    }
    return &EC->frames[EC->frameCount-1];
}

static inline Chunk *currentChunk(void) {
    return &getFrame()->closure->function->chunk;
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
            size_t instruction = frame->ip - function->chunk.code - 1;
            fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
            if (function->name == NULL) {
                fprintf(stderr, "script\n"); // top-level
            } else {
                char *fnName = function->name ? function->name->chars : "(anon)";
                fprintf(stderr, "%s()\n", fnName);
            }
        }
    }

    vm.hadError = true;
    resetStack();
}

void showUncaughtError(Value err) {
    ObjString *classNameObj = AS_INSTANCE(err)->klass->name;
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

    vm.hadError = true;
    resetStack();
}

// every new error value, when thrown, gets its backtrace set first
void setBacktrace(Value err) {
    VM_DEBUG("Setting backtrace");
    ASSERT(IS_AN_ERROR(err));
    Value ret = newArray();
    setProp(err, internedString("backtrace", 9), ret);
    int numECs = vm.v_ecs.length;
    VMExecContext *ctx;
    for (int i = numECs-1; i >= 0; i--) {
        ctx = vm.v_ecs.data[i];
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
    return IS_INSTANCE(val) && !IS_A_STRING(val);
}

// FIXME: use v_includedMods
static bool lookupMethod(ObjInstance *obj, ObjClass *klass, ObjString *propName, Value *ret, bool lookInGivenClass) {
    if (klass == obj->klass && obj->singletonKlass) {
        klass = obj->singletonKlass;
    }
    Value key = OBJ_VAL(propName);
    while (klass) {
        if ((!lookInGivenClass) && klass == obj->klass) {
            klass = klass->superclass; // FIXME: work in modules
            continue;
        }
        if (tableGet(&klass->methods, key, ret)) {
            return true;
        }
        klass = klass->superclass;
    }
    return false;
}

static InterpretResult vm_run(void);

static Value propertyGet(ObjInstance *obj, ObjString *propName) {
    Value ret;
    Obj *method = NULL;
    Obj *getter = NULL;
    if (tableGet(&obj->fields, OBJ_VAL(propName), &ret)) {
        return ret;
    } else if ((getter = instanceFindGetter(obj, propName))) {
        VM_DEBUG("getter found");
        callVMMethod(obj, OBJ_VAL(getter), 0, NULL);
        if (vm.hadError) {
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
        tableSet(&obj->fields, OBJ_VAL(propName), rval);
    }
}

static void defineMethod(ObjString *name) {
    Value method = peek(0); // function
    ASSERT(IS_CLOSURE(method));
    Value classOrMod = peek(1);
    ASSERT(IS_CLASS(classOrMod) || IS_MODULE(classOrMod));
    if (IS_CLASS(classOrMod)) {
        ObjClass *klass = AS_CLASS(classOrMod);
        const char *klassName = klass->name ? klass->name->chars : "(anon)";
        (void)klassName;
        VM_DEBUG("defining method '%s' in class '%s'", name->chars, klassName);
        ASSERT(tableSet(&klass->methods, OBJ_VAL(name), method));
    } else {
        ObjModule *mod = AS_MODULE(classOrMod);
        const char *modName = mod->name ? mod->name->chars : "(anon)";
        (void)modName;
        VM_DEBUG("defining method '%s' in module '%s'", name->chars, modName);
        ASSERT(tableSet(&mod->methods, OBJ_VAL(name), method));
    }
    pop(); // function
}

static void defineStaticMethod(ObjString *name) {
    Value method = peek(0); // function
    ASSERT(IS_CLOSURE(method));
    Value classOrMod = peek(1);
    ASSERT(IS_CLASS(classOrMod) || IS_MODULE(classOrMod));
    ObjClass *singletonClass = NULL;
    if (IS_CLASS(classOrMod)) {
        ObjClass *klass = AS_CLASS(classOrMod);
        singletonClass = classSingletonClass(klass);
    } else {
        ObjModule *mod = AS_MODULE(classOrMod);
        singletonClass = moduleSingletonClass(mod);
    }
    VM_DEBUG("defining static method '%s#%s'", singletonClass->name->chars, name->chars);
    ASSERT(tableSet(&singletonClass->methods, OBJ_VAL(name), method));
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
        ASSERT(tableSet(&klass->getters, OBJ_VAL(name), method));
    } else {
        ObjModule *mod = AS_MODULE(classOrMod);
        VM_DEBUG("defining getter '%s'", name->chars);
        ASSERT(tableSet(&mod->getters, OBJ_VAL(name), method));
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
        ASSERT(tableSet(&klass->setters, OBJ_VAL(name), method));
    } else {
        ObjModule *mod = AS_MODULE(classOrMod);
        VM_DEBUG("defining setter '%s'", name->chars);
        ASSERT(tableSet(&mod->setters, OBJ_VAL(name), method));
    }
    pop(); // function
}

// Call method on instance, args are NOT expected to be pushed on to stack by
// caller, nor is the instance. `argCount` does not include the implicit instance argument.
// Return value is pushed to stack and returned.
Value callVMMethod(ObjInstance *instance, Value callable, int argCount, Value *args) {
    VM_DEBUG("Calling VM method");
    push(OBJ_VAL(instance));
    for (int i = 0; i < argCount; i++) {
        ASSERT(args);
        push(args[i]);
    }
    VM_DEBUG("call begin");
    callCallable(callable, argCount, true, NULL); // pushes return value to stack
    VM_DEBUG("call end");
    return peek(0);
}

static void unwindErrInfo(CallFrame *frame) {
    ErrTagInfo *info = vm.errInfo;
    while (info && info->frame == frame) {
        ErrTagInfo *prev = info->prev;
        FREE(ErrTagInfo, info);
        info = prev;
    }
    vm.errInfo = info;
}


void popFrame(void) {
    DBG_ASSERT(vm.inited);
    ASSERT(EC->frameCount >= 1);
    VM_DEBUG("popping callframe (%s)", getFrame()->isCCall ? "native" : "non-native");
    CallFrame *frame = getFrame();
    unwindErrInfo(frame);
    if (frame->isCCall) {
        ASSERT(inCCall > 0);
        inCCall--;
        if (inCCall == 0) {
            vec_clear(&vm.stackObjects);
        }
    }
    memset(frame, 0, sizeof(*frame));
    EC->frameCount--;
    frame = getFrameOrNull();
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
    inCCall++;
}

// sets up VM/C call jumpbuf if not set
static void captureNativeError(void) {
    if (inCCall == 0) {
        VM_DEBUG("%s", "Setting VM/C error jump buffer");
        int jumpRes = setjmp(CCallJumpBuf);
        if (jumpRes == JUMP_SET) { // jump is set, prepared to enter C land
            return;
        } else { // C call longjmped here from throwError()
            ASSERT(inCCall > 0);
            ASSERT(cCallThrew);
            cCallThrew = false;
            returnedFromNativeErr = true;
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
        return false;
    }
    return true;
}

// Arguments are expected to be pushed on to stack by caller. Argcount
// does NOT include the instance argument, ex: a method with no arguments will have an
// argCount of 0. If the callable is a class (constructor), this function creates the
// new instance and puts it in the proper spot in the stack. The return value
// is pushed to the stack.
static bool doCallCallable(Value callable, int argCount, bool isMethod, CallInfo *callInfo) {
    ObjClosure *closure = NULL;
    Value instanceVal;
    if (IS_CLOSURE(callable)) {
        closure = AS_CLOSURE(callable);
        if (!isMethod) {
            EC->stackTop[-argCount - 1] = callable;
        }
    } else if (IS_CLASS(callable)) {
        ObjClass *klass = AS_CLASS(callable);
        const char *klassName = klass->name ? klass->name->chars : "(anon)";
        (void)klassName;
        VM_DEBUG("calling callable class %s", klassName);
        ObjInstance *instance = newInstance(klass);
        instanceVal = OBJ_VAL(instance);
        /*ASSERT(IS_CLASS(EC->stackTop[-argCount - 1])); this holds true if the # of args is correct for the function */
        EC->stackTop[-argCount - 1] = instanceVal; // first argument is instance, replaces class object
        // Call the initializer, if there is one.
        Value initializer;
        Obj *init = instanceFindMethod(instance, vm.initString);
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
                nativeInit->function(argCount+1, EC->stackTop-argCount-1);
                newFrame->slots = EC->stackTop-argCount-1;
                if (returnedFromNativeErr) {
                    returnedFromNativeErr = false;
                    VM_DEBUG("native initializer returned from error");
                    vec_clear(&vm.stackObjects);
                    while (getFrame() >= newFrame) {
                        popFrame();
                    }
                    ASSERT(inCCall == 0);
                    throwError(vm.lastErrorThrown); // re-throw inside VM
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
        } else if (argCount > 0) {
            throwArgErrorFmt("Expected 0 arguments (Object#init) but got %d.", argCount);
            return false;
        } else {
            return true; // new instance is on the top of the stack
        }
    } else if (IS_BOUND_METHOD(callable)) {
        VM_DEBUG("calling bound method with %d args", argCount);
        ObjBoundMethod *bmethod = AS_BOUND_METHOD(callable);
        Obj *callable = bmethod->callable; // native function or user-defined function (ObjClosure)
        instanceVal = bmethod->receiver;
        EC->stackTop[-argCount - 1] = instanceVal;
        return doCallCallable(OBJ_VAL(callable), argCount, true, callInfo);
    } else if (IS_NATIVE_FUNCTION(callable)) {
        VM_DEBUG("Calling native %s with %d args", isMethod ? "method" : "function", argCount);
        ObjNative *native = AS_NATIVE_FUNCTION(callable);
        if (isMethod) argCount++;
        captureNativeError();
        pushNativeFrame(native);
        CallFrame *newFrame = getFrame();
        Value val = native->function(argCount, EC->stackTop-argCount);
        newFrame->slots = EC->stackTop-argCount;
        if (returnedFromNativeErr) {
            VM_DEBUG("Returned from native function with error");
            returnedFromNativeErr = false;
            while (getFrame() >= newFrame) {
                popFrame();
            }
            ASSERT(inCCall == 0);
            throwError(vm.lastErrorThrown); // re-throw inside VM
            return false;
        } else {
            VM_DEBUG("Returned from native function without error");
            EC->stackTop = getFrame()->slots;
            popFrame();
            push(val);
        }
        return true;
    } else {
        UNREACHABLE("bad callable value given to callCallable");
    }

    if (EC->frameCount >= FRAMES_MAX) {
        errorPrintScriptBacktrace("Stack overflow.");
        return false;
    }

    VM_DEBUG("doCallCallable found closure");
    // non-native function/method call
    ASSERT(closure);
    ObjFunction *func = closure->function;
    bool arityOK = checkFunctionArity(func, argCount);
    if (!arityOK) return false;

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

    int parentStart = getFrame()->ip - getFrame()->closure->function->chunk.code - 2;
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
    if (funcOffset > 0) {
        VM_DEBUG("Func offset due to optargs: %d", (int)funcOffset);
    }
    frame->closure = closure;
    frame->ip = closure->function->chunk.code + funcOffset;
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
    int lenBefore = vm.stackObjects.length;
    bool ret = doCallCallable(callable, argCount, isMethod, info);
    int lenAfter = vm.stackObjects.length;

    // allow collection of new stack-created objects if they're not rooted now
    for (int i = lenBefore; i < lenAfter; i++) {
        (void)vec_pop(&vm.stackObjects);
    }

    return ret;
}

/**
 * When thrown (OP_THROW), find any surrounding try { } catch { } block with
 * the proper class to catch.
 */
static bool findThrowJumpLoc(ObjClass *klass, uint8_t **ipOut, CatchTable **rowFound) {
    CatchTable *tbl = currentChunk()->catchTbl;
    CatchTable *row = tbl;
    int currentIpOff = (int)(getFrame()->ip - currentChunk()->code);
    bool poppedEC = false;
    VM_DEBUG("findthrowjumploc");
    while (row || EC->frameCount >= 1) {
        VM_DEBUG("framecount: %d, num ECs: %d", EC->frameCount, vm.v_ecs.length);
        if (row == NULL) { // pop a call frame
            VM_DEBUG("row null");
            if (vm.v_ecs.length == 0 || (vm.v_ecs.length == 1 && EC->frameCount == 1)) {
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
    ErrTagInfo *cur = vm.errInfo;
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
    vm.lastErrorThrown = self;
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
    if (inCCall > 0) {
        VM_DEBUG("throwing error from C call, longjmping");
        ASSERT(!cCallThrew);
        cCallThrew = true;
        longjmp(CCallJumpBuf, JUMP_PERFORMED);
    }
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
    vm.errInfo = vm.errInfo->prev;
}

void unsetErrInfo(void) {
    vm.lastErrorThrown = NIL_VAL;
    ASSERT(vm.errInfo);
    vm.errInfo = vm.errInfo->prev;
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
    vm.lastErrorThrown = err;
    unhideFromGC((Obj*)buf);
    throwError(err);
    UNREACHABLE("thrown");
}

void printVMStack(FILE *f) {
    if (EC->stackTop == EC->stack && vm.v_ecs.length == 1) {
        fprintf(f, "[DEBUG %d]: Stack: empty\n", vmRunLvl);
        return;
    }
    VMExecContext *ec = NULL; int i = 0;
    int numCallFrames = VMNumCallFrames();
    int numStackFrames = VMNumStackFrames();
    fprintf(f, "[DEBUG %d]: Stack (%d stack frames, %d call frames):\n", vmRunLvl, numStackFrames, numCallFrames);
    // print VM stack values from bottom of stack to top
    fprintf(f, "[DEBUG %d]: ", vmRunLvl);
    int callFrameIdx = 0;
    vec_foreach(&vm.v_ecs, ec, i) {
        for (Value *slot = ec->stack; slot < ec->stackTop; slot++) {
            if (IS_OBJ(*slot) && (AS_OBJ(*slot)->type <= OBJ_T_NONE)) {
                fprintf(stderr, "[DEBUG %d]: Broken object pointer: %p\n", vmRunLvl, AS_OBJ(*slot));
                ASSERT(0);
            }
            if (ec->frames[callFrameIdx].slots == slot) {
                fprintf(f, "(CF %d)", callFrameIdx+1);
                callFrameIdx++;
            }
            fprintf(f, "[ ");
            printValue(f, *slot, false);
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
        printValue(stderr, *local, false);
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
    default:
        return NULL;
    }
}

/**
 * Run the VM's instructions.
 */
static InterpretResult vm_run() {
    if (CLOX_OPTION_T(parseOnly) || CLOX_OPTION_T(compileOnly)) {
        return INTERPRET_OK;
    }

    if (!rootVMLoopJumpBufSet) {
        ASSERT(vmRunLvl == 0);
        int jumpRes = setjmp(rootVMLoopJumpBuf);
        rootVMLoopJumpBufSet = true;
        if (jumpRes == JUMP_SET) { // jump is set
            VM_DEBUG("VM set rootVMLoopJumpBuf");
        } else {
            VM_DEBUG("VM caught error in rootVMLoopJumpBuf");
            showUncaughtError(vm.lastErrorThrown);
            return INTERPRET_RUNTIME_ERROR;
        }
    }
    vmRunLvl++;
    Chunk *ch = currentChunk();
    if (ch->catchTbl != NULL) {
        CallFrame *f = getFrame();
        int jumpRes = setjmp(f->jmpBuf);
        if (jumpRes == JUMP_SET) {
            f->jmpBufSet = true;
            VM_DEBUG("VM set catch table for call frame (vm_run lvl %d)", vmRunLvl-1);
        } else {
            VM_DEBUG("VM caught error for call frame (vm_run lvl %d)", vmRunLvl-1);
            vm.hadError = false;
            // stack is already unwound to proper frame
        }
    }
#define READ_BYTE() (*getFrame()->ip++)
#define READ_CONSTANT() (ch->constants.values[READ_BYTE()])
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
          throwErrorFmt(lxTypeErrClass, "binary operation type error, lhs=%s, rhs=%s", typeOfVal(a), typeOfVal(b));\
      }\
    } while (0)

  /*fprintf(stderr, "VM run level: %d\n", vmRunLvl);*/
  for (;;) {
      if (vm.hadError) {
          vmRunLvl--;
          return INTERPRET_RUNTIME_ERROR;
      }
      if (vm.exited) {
          vmRunLvl--;
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
      case OP_BITXOR:   BINARY_OP(&,OP_BITAND, int); break;
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
            printValue(stdout, val, true);
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
          getFrame()->slots[slot] = unpackValue(peek(0), unpackIdx); // locals are popped at end of scope by VM
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
          if (lastSplatNumArgs > 0) {
              numArgs += (lastSplatNumArgs-1);
              lastSplatNumArgs = -1;
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
          CallInfo *callInfo = internalGetData(AS_INTERNAL(callInfoVal));
          if (lastSplatNumArgs > 0) {
              numArgs += (lastSplatNumArgs-1);
              lastSplatNumArgs = -1;
          }
          Value instanceVal = peek(numArgs);
          if (IS_INSTANCE(instanceVal)) {
              ObjInstance *inst = AS_INSTANCE(instanceVal);
              Obj *callable = instanceFindMethod(inst, mname);
              if (!callable) {
                  ObjString *className = inst->klass->name;
                  const char *classStr = className->chars ? className->chars : "(anon)";
                  throwErrorFmt(lxErrClass, "instance method '%s#%s' not found", classStr, mname->chars);
              }
              setThis(numArgs);
              callCallable(OBJ_VAL(callable), numArgs, true, callInfo);
          } else if (IS_CLASS(instanceVal)) {
              ObjClass *klass = AS_CLASS(instanceVal);
              Obj *callable = classFindStaticMethod(klass, mname);
              if (!callable) {
                  ObjString *className = klass->name;
                  const char *classStr = className ? className->chars : "(anon)";
                  throwErrorFmt(lxErrClass, "class method '%s.%s' not found", classStr, mname->chars);
              }
              EC->stackTop[-numArgs-1] = instanceVal;
              setThis(numArgs);
              callCallable(OBJ_VAL(callable), numArgs, true, callInfo);
          } else if (IS_MODULE(instanceVal)) {
              ObjModule *mod = AS_MODULE(instanceVal);
              Obj *callable = moduleFindStaticMethod(mod, mname);
              if (!callable) {
                  ObjString *modName = mod->name;
                  const char *modStr = modName ? modName->chars : "(anon)";
                  throwErrorFmt(lxErrClass, "module method '%s.%s' not found", modStr, mname->chars);
              }
              EC->stackTop[-numArgs-1] = instanceVal;
              setThis(numArgs);
              callCallable(OBJ_VAL(callable), numArgs, true, callInfo);
          } else {
              throwErrorFmt(lxTypeErrClass, "Tried to invoke method on non-instance (type=%s)", typeOfVal(instanceVal));
          }
          ASSERT_VALID_STACK();
          break;
      }
      case OP_GET_THIS: {
          ASSERT(vm.thisValue);
          push(*vm.thisValue);
          break;
      }
      case OP_SPLAT_ARRAY: {
          Value ary = pop();
          if (!IS_AN_ARRAY(ary)) {
              throwErrorFmt(lxTypeErrClass, "Splatted expression must evaluate to an Array (type=%s)",
                      typeOfVal(ary));
          }
          lastSplatNumArgs = ARRAY_SIZE(ary);
          for (int i = 0; i < lastSplatNumArgs; i++) {
              push(ARRAY_GET(ary, i));
          }
          break;
      }
      case OP_GET_SUPER: {
          // FIXME: top of stack should contain class or module of the 'super' call
          Value methodName = READ_CONSTANT();
          ASSERT(vm.thisValue);
          Value instanceVal = *vm.thisValue;
          ASSERT(IS_INSTANCE(instanceVal)); // FIXME: get working for classes (singleton methods)
          ObjClass *klass = AS_INSTANCE(instanceVal)->klass;
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
          vmRunLvl--;
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
          ASSERT(isIterator(iterator)); // FIXME: throw TypeError
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
          if (!isThrowable(throwable)) {
              throwErrorFmt(lxTypeErrClass, "Tried to throw unthrowable value, must be an instance. "
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
          push(OBJ_VAL(dupString(AS_STRING(strLit))));
          bool ret = callCallable(peek(1), 1, false, NULL);
          ASSERT(ret); // the string instance is pushed to top of stack
          if (isStatic == 1) {
              objFreeze(AS_OBJ(peek(0)));
          }
          break;
      }
      // exit interpreter, or evaluation context if in eval() or
      // loadScript/requireScript
      case OP_LEAVE: {
          if (!isInEval() && !isInLoadedScript()) vm.exited = true;
          vmRunLvl--;
          return INTERPRET_OK;
      }
      default:
          errorPrintScriptBacktrace("Unknown opcode instruction: %s (%d)", opName(instruction), instruction);
          vmRunLvl--;
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
    tableSet(&EC->roGlobals, OBJ_VAL(vm.fileString), newStringInstance(file));

    if (filename[0] == pathSeparator) {
        char *lastSep = rindex(filename, pathSeparator);
        int len = lastSep - filename;
        ObjString *dir = copyString(filename, len);
        tableSet(&EC->roGlobals, OBJ_VAL(vm.dirString), newStringInstance(dir));
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
        rethrowErrInfo(vm.errInfo);
        UNREACHABLE_RETURN(INTERPRET_RUNTIME_ERROR);
    } else {
        return INTERPRET_OK;
    }
}

static Value doVMEval(const char *src, const char *filename, int lineno, bool throwOnErr) {
    CallFrame *oldFrame = getFrame();
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int oldOpts = compilerOpts.noRemoveUnusedExpressions;
    compilerOpts.noRemoveUnusedExpressions = true;
    push_EC();
    VMExecContext *ectx = EC;
    ectx->evalContext = true;
    resetStack();
    int compile_res = compile_src(src, &chunk, &err);
    compilerOpts.noRemoveUnusedExpressions = oldOpts;

    if (compile_res != 0) {
        VM_DEBUG("compile error in eval");
        pop_EC();
        ASSERT(getFrame() == oldFrame);
        freeChunk(&chunk);
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
    frame->ip = chunk.code;
    frame->slots = EC->stack;
    ObjFunction *func = newFunction(&chunk, NULL);
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
        vm.hadError = true;
    }
    Value val = *vm.lastValue;
    VM_DEBUG("eval finished: error: %d", vm.hadError ? 1 : 0);
    // `EC != ectx` if an error occured in the eval, and propagated out
    // due to being caught in a surrounding context or never being caught.
    if (EC == ectx) pop_EC();
    ASSERT(getFrame() == oldFrame);
    if (result == INTERPRET_OK) {
        return val;
    } else {
        if (throwOnErr) {
            rethrowErrInfo(vm.errInfo);
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
    while (getFrame() != info->frame) {
        VM_DEBUG("popping callframe from unwind");
        popFrame();
    }
    while (vm.errInfo != info) {
        VM_DEBUG("freeing Errinfo");
        ASSERT(vm.errInfo);
        ErrTagInfo *prev = vm.errInfo->prev;
        ASSERT(prev);
        FREE(ErrTagInfo, vm.errInfo);
        vm.errInfo = prev;
    }
}

void *vm_protect(vm_cb_func func, void *arg, ObjClass *errClass, ErrTag *status) {
    addErrInfo(errClass);
    ErrTagInfo *errInfo = vm.errInfo;
    int jmpres = 0;
    if ((jmpres = setjmp(errInfo->jmpBuf)) == JUMP_SET) {
        *status = TAG_NONE;
        VM_DEBUG("vm_protect before func");
        void *res = func(arg);
        ErrTagInfo *prev = errInfo->prev;
        FREE(ErrTagInfo, errInfo);
        vm.errInfo = prev;
        VM_DEBUG("vm_protect after func");
        return res;
    } else if (jmpres == JUMP_PERFORMED) {
        VM_DEBUG("vm_protect got to longjmp");
        ASSERT(errInfo == vm.errInfo);
        unwindJumpRecover(errInfo);
        errInfo->status = TAG_RAISE;
        errInfo->caughtError = vm.lastErrorThrown;
        *status = TAG_RAISE;
    } else {
        fprintf(stderr, "vm_protect: error from setjmp");
        UNREACHABLE("setjmp error");
    }
    return NULL;
}

ErrTagInfo *addErrInfo(ObjClass *errClass) {
    struct ErrTagInfo *info = ALLOCATE(ErrTagInfo, 1);
    info->status = TAG_NONE;
    info->errClass = errClass;
    info->frame = getFrame();
    info->prev = vm.errInfo;
    vm.errInfo = info;
    info->caughtError = NIL_VAL;
    return info;
}

void runAtExitHooks(void) {
    vm.exited = false;
    ObjClosure *func = NULL;
    int i = 0;
    vec_foreach_rev(&vm.exitHandlers, func, i) {
        callCallable(OBJ_VAL(func), 0, false, NULL);
        pop();
    }
    vm.exited = true;
}

// FIXME: only exit the current thread. Stop the VM only if it's main thread.
NORETURN void stopVM(int status) {
    runAtExitHooks();
    resetStack();
    freeVM();
    _exit(status);
}

void acquireGVL(void) {
    pthread_t tid = pthread_self(); (void)tid;
    THREAD_DEBUG(3, "thread %lu locking GVL...", (unsigned long)tid);
    pthread_mutex_lock(&vm.GVLock);
    // TODO: set vm.curThread here
    THREAD_DEBUG(3, "thread %lu locked GVL", (unsigned long)tid);
}

void releaseGVL(void) {
    pthread_t tid = pthread_self(); (void)tid;
    THREAD_DEBUG(3, "thread %lu unlocking GVL", (unsigned long)tid);
    // TODO: set vm.curThread to NULL here
    pthread_mutex_unlock(&vm.GVLock);
}
