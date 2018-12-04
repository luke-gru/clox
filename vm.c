#include <stdio.h>
#include "common.h"
#include "vm.h"
#include "debug.h"
#include "options.h"
#include "runtime.h"
#include "memory.h"
#include "compiler.h"
#include "nodes.h"
#include <setjmp.h>

VM vm;

#ifndef NDEBUG
#define VM_DEBUG(...) vm_debug(__VA_ARGS__)
#define VM_WARN(...) vm_warn(__VA_ARGS__)
#else
#define VM_DEBUG(...) (void(0))
#define VM_WARN(...) (void(0))
#endif

#define EC (vm.ec)

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
static void vm_warn(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "[Warning]: ");
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
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

static void defineNativeFunctions() {
    ObjString *clockName = internedString("clock", 5);
    ObjNative *clockFn = newNative(clockName, lxClock);
    tableSet(&vm.globals, OBJ_VAL(clockName), OBJ_VAL(clockFn));

    ObjString *typeofName = internedString("typeof", 6);
    ObjNative *typeofFn = newNative(typeofName, lxTypeof);
    tableSet(&vm.globals, OBJ_VAL(typeofName), OBJ_VAL(typeofFn));

    ObjString *loadScriptName = internedString("loadScript", 10);
    ObjNative *loadScriptFn = newNative(loadScriptName, lxLoadScript);
    tableSet(&vm.globals, OBJ_VAL(loadScriptName), OBJ_VAL(loadScriptFn));

    ObjString *reqScriptName = internedString("requireScript", 13);
    ObjNative *reqScriptFn = newNative(reqScriptName, lxRequireScript);
    tableSet(&vm.globals, OBJ_VAL(reqScriptName), OBJ_VAL(reqScriptFn));

    ObjString *debuggerName = internedString("debugger", 8);
    ObjNative *debuggerFn = newNative(debuggerName, lxDebugger);
    tableSet(&vm.globals, OBJ_VAL(debuggerName), OBJ_VAL(debuggerFn));

    ObjString *evalName = internedString("eval", 4);
    ObjNative *evalFn = newNative(evalName, lxEval);
    tableSet(&vm.globals, OBJ_VAL(evalName), OBJ_VAL(evalFn));

    ObjString *forkName = internedString("fork", 4);
    ObjNative *forkFn = newNative(forkName, lxFork);
    tableSet(&vm.globals, OBJ_VAL(forkName), OBJ_VAL(forkFn));

    ObjString *waitpidName = internedString("waitpid", 7);
    ObjNative *waitpidFn = newNative(waitpidName, lxWaitpid);
    tableSet(&vm.globals, OBJ_VAL(waitpidName), OBJ_VAL(waitpidFn));

    ObjString *sleepName = internedString("sleep", 5);
    ObjNative *sleepFn = newNative(sleepName, lxSleep);
    tableSet(&vm.globals, OBJ_VAL(sleepName), OBJ_VAL(sleepFn));

    ObjString *exitName = internedString("exit", 4);
    ObjNative *exitFn = newNative(exitName, lxExit);
    tableSet(&vm.globals, OBJ_VAL(exitName), OBJ_VAL(exitFn));
}

// Builtin classes:
ObjClass *lxObjClass;
ObjClass *lxStringClass;
ObjClass *lxClassClass;
ObjClass *lxModuleClass;
ObjClass *lxAryClass;
ObjClass *lxMapClass;
ObjClass *lxErrClass;
ObjClass *lxArgErrClass;
ObjClass *lxTypeErrClass;
ObjClass *lxNameErrClass;
ObjClass *lxFileClass;
Value lxLoadPath;

static void defineNativeClasses() {
    // class Object
    ObjString *objClassName = internedString("Object", 6);
    ObjClass *objClass = newClass(objClassName, NULL);
    tableSet(&vm.globals, OBJ_VAL(objClassName), OBJ_VAL(objClass));

    ObjNative *objDupNat = newNative(internedString("dup", 3), lxObjectDup);
    tableSet(&objClass->methods, OBJ_VAL(internedString("dup", 3)), OBJ_VAL(objDupNat));

    ObjNative *objGetClassNat = newNative(internedString("_class", 6), lxObjectGetClass);
    tableSet(&objClass->getters, OBJ_VAL(internedString("_class", 6)), OBJ_VAL(objGetClassNat));

    ObjNative *objGetObjectIdNat = newNative(internedString("objectId", 8), lxObjectGetObjectId);
    tableSet(&objClass->getters, OBJ_VAL(internedString("objectId", 8)), OBJ_VAL(objGetObjectIdNat));

    lxObjClass = objClass;

    // class Module
    ObjString *modClassName = internedString("Module", 6);
    ObjClass *modClass = newClass(modClassName, objClass);
    tableSet(&vm.globals, OBJ_VAL(modClassName), OBJ_VAL(modClass));

    lxModuleClass = modClass;

    // class Class
    ObjString *classClassName = internedString("Class", 5);
    ObjClass *classClass = newClass(classClassName, objClass);
    tableSet(&vm.globals, OBJ_VAL(classClassName), OBJ_VAL(classClass));

    lxClassClass = classClass;

    // restore `klass` property of above-created classes, since <class Class>
    // is now created
    objClass->klass = classClass;
    modClass->klass = classClass;
    classClass->klass = classClass;

    // class String
    ObjString *stringClassName = internedString("String", 6);
    ObjClass *stringClass = newClass(stringClassName, objClass);
    tableSet(&vm.globals, OBJ_VAL(stringClassName), OBJ_VAL(stringClass));

    ObjNative *stringInitNat = newNative(internedString("init", 4), lxStringInit);
    tableSet(&stringClass->methods, OBJ_VAL(internedString("init", 4)), OBJ_VAL(stringInitNat));

    ObjNative *stringtoStringNat = newNative(internedString("toString", 8), lxStringToString);
    tableSet(&stringClass->methods, OBJ_VAL(internedString("toString", 8)), OBJ_VAL(stringtoStringNat));

    ObjNative *stringOpAddNat = newNative(internedString("opAdd", 5), lxStringOpAdd);
    tableSet(&stringClass->methods, OBJ_VAL(internedString("opAdd", 5)), OBJ_VAL(stringOpAddNat));

    ObjNative *stringPushNat = newNative(internedString("push", 4), lxStringPush);
    tableSet(&stringClass->methods, OBJ_VAL(internedString("push", 4)), OBJ_VAL(stringPushNat));

    ObjNative *stringDupNat = newNative(internedString("dup", 3), lxStringDup);
    tableSet(&stringClass->methods, OBJ_VAL(internedString("dup", 3)), OBJ_VAL(stringDupNat));

    lxStringClass = stringClass;

    // class Class
    ObjNative *classInitNat = newNative(internedString("init", 4), lxClassInit);
    tableSet(&classClass->methods, OBJ_VAL(internedString("init", 4)), OBJ_VAL(classInitNat));

    ObjNative *classIncludeNat = newNative(internedString("include", 7), lxClassInclude);
    tableSet(&classClass->methods, OBJ_VAL(internedString("include", 7)), OBJ_VAL(classIncludeNat));

    ObjNative *classGetSuperclassNat = newNative(internedString("_superClass", 11), lxClassGetSuperclass);
    tableSet(&classClass->getters, OBJ_VAL(internedString("_superClass", 11)), OBJ_VAL(classGetSuperclassNat));

    ObjNative *classGetNameNat = newNative(internedString("name", 4), lxClassGetName);
    tableSet(&classClass->getters, OBJ_VAL(internedString("name", 4)), OBJ_VAL(classGetNameNat));

    // class Array
    ObjString *arrayClassName = internedString("Array", 5);
    ObjClass *arrayClass = newClass(arrayClassName, objClass);
    tableSet(&vm.globals, OBJ_VAL(arrayClassName), OBJ_VAL(arrayClass));

    lxAryClass = arrayClass;

    ObjNative *aryInitNat = newNative(internedString("init", 4), lxArrayInit);
    tableSet(&arrayClass->methods, OBJ_VAL(internedString("init", 4)), OBJ_VAL(aryInitNat));

    ObjNative *aryPushNat = newNative(internedString("push", 4), lxArrayPush);
    tableSet(&arrayClass->methods, OBJ_VAL(internedString("push", 4)), OBJ_VAL(aryPushNat));

    ObjNative *aryIdxGetNat = newNative(internedString("indexGet", 8), lxArrayIndexGet);
    tableSet(&arrayClass->methods, OBJ_VAL(internedString("indexGet", 8)), OBJ_VAL(aryIdxGetNat));

    ObjNative *aryIdxSetNat = newNative(internedString("indexSet", 8), lxArrayIndexSet);
    tableSet(&arrayClass->methods, OBJ_VAL(internedString("indexSet", 8)), OBJ_VAL(aryIdxSetNat));

    ObjNative *aryToStringNat = newNative(internedString("toString", 8), lxArrayToString);
    tableSet(&arrayClass->methods, OBJ_VAL(internedString("toString", 8)), OBJ_VAL(aryToStringNat));

    // class Map
    ObjString *mapClassName = internedString("Map", 3);
    ObjClass *mapClass = newClass(mapClassName, objClass);
    tableSet(&vm.globals, OBJ_VAL(mapClassName), OBJ_VAL(mapClass));

    lxMapClass = mapClass;

    ObjNative *mapInitNat = newNative(internedString("init", 4), lxMapInit);
    tableSet(&mapClass->methods, OBJ_VAL(internedString("init", 4)), OBJ_VAL(mapInitNat));

    ObjNative *mapIdxGetNat = newNative(internedString("indexGet", 8), lxMapIndexGet);
    tableSet(&mapClass->methods, OBJ_VAL(internedString("indexGet", 8)), OBJ_VAL(mapIdxGetNat));

    ObjNative *mapIdxSetNat = newNative(internedString("indexSet", 8), lxMapIndexSet);
    tableSet(&mapClass->methods, OBJ_VAL(internedString("indexSet", 8)), OBJ_VAL(mapIdxSetNat));

    ObjNative *mapKeysNat = newNative(internedString("keys", 4), lxMapKeys);
    tableSet(&mapClass->methods, OBJ_VAL(internedString("keys", 4)), OBJ_VAL(mapKeysNat));

    ObjNative *mapValuesNat = newNative(internedString("values", 6), lxMapValues);
    tableSet(&mapClass->methods, OBJ_VAL(internedString("values", 6)), OBJ_VAL(mapValuesNat));

    ObjNative *mapToStringNat = newNative(internedString("toString", 8), lxMapToString);
    tableSet(&mapClass->methods, OBJ_VAL(internedString("toString", 8)), OBJ_VAL(mapToStringNat));

    // class Error
    ObjString *errClassName = internedString("Error", 5);
    ObjClass *errClass = newClass(errClassName, objClass);
    tableSet(&vm.globals, OBJ_VAL(errClassName), OBJ_VAL(errClass));

    lxErrClass = errClass;

    ObjNative *errInitNat = newNative(internedString("init", 4), lxErrInit);
    tableSet(&errClass->methods, OBJ_VAL(internedString("init", 4)), OBJ_VAL(errInitNat));

    // class ArgumentError
    ObjString *argErrClassName = internedString("ArgumentError", 13);
    ObjClass *argErrClass = newClass(argErrClassName, errClass);
    tableSet(&vm.globals, OBJ_VAL(argErrClassName), OBJ_VAL(argErrClass));

    lxArgErrClass = argErrClass;

    // class TypeError
    ObjString *typeErrClassName = internedString("TypeError", 9);
    ObjClass *typeErrClass = newClass(typeErrClassName, errClass);
    tableSet(&vm.globals, OBJ_VAL(typeErrClassName), OBJ_VAL(typeErrClass));

    lxTypeErrClass = typeErrClass;

    // class NameError
    ObjString *nameErrClassName = internedString("NameError", 9);
    ObjClass *nameErrClass = newClass(nameErrClassName, errClass);
    tableSet(&vm.globals, OBJ_VAL(nameErrClassName), OBJ_VAL(nameErrClass));

    lxNameErrClass = nameErrClass;

    // class File
    ObjString *fileClassName = internedString("File", 4);
    ObjClass *fileClass = newClass(fileClassName, objClass);
    tableSet(&vm.globals, OBJ_VAL(fileClassName), OBJ_VAL(fileClass));
    ObjClass *fileClassStatic = classSingletonClass(fileClass);

    lxFileClass = fileClass;

    ObjNative *fileReadNat = newNative(internedString("read", 4), lxFileReadStatic);
    tableSet(&fileClassStatic->methods, OBJ_VAL(internedString("read", 4)), OBJ_VAL(fileReadNat));
}

static void defineGlobalVariables() {
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
            arrayPush(lxLoadPath, OBJ_VAL(str));
            beg = end+1;
        }
    }
}


static jmp_buf CCallJumpBuf;
bool inCCall;
static bool cCallThrew = false;
static bool returnedFromNativeErr = false;
static bool runUntilReturn = false;
static int lastSplatNumArgs = -1;

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

// reset (clear) value stack for current execution context
void resetStack() {
    EC->stackTop = EC->stack;
    EC->frameCount = 0;
}

#define FIRST_GC_THRESHHOLD (1024*1024)

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

    initDebugger(&vm.debugger);

    vm.lastErrorThrown = NIL_VAL;
    vm.hadError = false;
    inCCall = false;
    cCallThrew = false;
    returnedFromNativeErr = false;
    runUntilReturn = false;
    curLine = 1;
    memset(&CCallJumpBuf, 0, sizeof(CCallJumpBuf));

    memset(&rootVMLoopJumpBuf, 0, sizeof(rootVMLoopJumpBuf));
    rootVMLoopJumpBufSet = false;

    defineGlobalVariables();
    resetStack();
    turnGCOn();
    VM_DEBUG("initVM() end");
}

void freeVM() {
    /*fprintf(stderr, "VM run level: %d\n", vmRunLvl);*/
    if (!vm.inited) {
        VM_WARN("freeVM: VM not yet initialized");
        return;
    }
    VM_DEBUG("freeVM() start");
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    vm.initString = NULL;
    vm.fileString = NULL;
    vm.dirString = NULL;
    vm.hadError = false;
    vm.printBuf = NULL;
    vm.printToStdout = true;
    vm.lastValue = NULL;
    vm.thisValue = NULL;
    vm.grayStack = NULL;
    vm.openUpvalues = NULL;
    vec_deinit(&vm.hiddenObjs);
    vec_deinit(&vm.loadedScripts);

    freeDebugger(&vm.debugger);

    inCCall = false;
    cCallThrew = false;
    returnedFromNativeErr = false;
    memset(&CCallJumpBuf, 0, sizeof(CCallJumpBuf));
    curLine = 1;

    memset(&rootVMLoopJumpBuf, 0, sizeof(rootVMLoopJumpBuf));
    rootVMLoopJumpBufSet = false;

    vec_deinit(&vm.stackObjects);
    freeObjects();
    vm.objects = NULL;

    vec_clear(&vm.v_ecs);
    vm.ec = NULL;
    vm.inited = false;
    vm.exited = false;
    VM_DEBUG("freeVM() end");
}


int VMNumStackFrames() {
    VMExecContext *firstEC = vec_first(&vm.v_ecs);
    return EC->stackTop - firstEC->stack;
}

int VMNumCallFrames() {
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
        if (strcmp(fname, AS_CSTRING(*loaded)) == 0) {
            return true;
        }
    }
    return false;
}

#define ASSERT_VALID_STACK(...) ASSERT(EC->stackTop >= EC->stack)

static bool isOpStackEmpty() {
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

Value pop() {
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

Value *getLastValue() {
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
    return BOOL_VAL(true);
}

static inline Value falseValue() {
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
        return strcmp(VAL_TO_STRING(lhs)->chars, VAL_TO_STRING(rhs)->chars);
    }

    // TODO: error out
    return -2;
}

static bool isValueOpEqual(Value lhs, Value rhs) {
    if (lhs.type != rhs.type) {
        return false;
    }
    if (IS_A_STRING(lhs) && IS_A_STRING(rhs)) {
        return strcmp(VAL_TO_STRING(lhs)->chars, VAL_TO_STRING(rhs)->chars) == 0;
    } else if (IS_OBJ(lhs)) { // 2 objects, same pointers to Obj are equal
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

static inline CallFrame *getFrame() {
    ASSERT(EC->frameCount >= 1);
    return &EC->frames[EC->frameCount-1];
}

static inline Chunk *currentChunk() {
    return &getFrame()->closure->function->chunk;
}

void diePrintBacktrace(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

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
    char *className = AS_INSTANCE(err)->klass->name->chars;
    if (!className) { className = "(anon)"; }
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
            ObjString *outBuf = hiddenString("", 0);
            Value out = newStringInstance(outBuf);
            if (frame->isCCall) {
                ObjNative *nativeFunc = frame->nativeFunc;
                pushCStringFmt(outBuf, "%s:%d in ", file->chars, line);
                pushCStringFmt(outBuf, "<%s (native)>\n",
                        nativeFunc->name->chars);
            } else {
                ObjFunction *function = frame->closure->function;
                pushCStringFmt(outBuf, "%s:%d in ", file->chars, line);
                if (function->name == NULL) {
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
}

static inline bool isThrowable(Value val) {
    return IS_INSTANCE(val) && !IS_A_STRING(val);
}

// FIXME: use v_includedMods
static bool lookupGetter(ObjInstance *obj, ObjString *propName, Value *ret) {
    ObjClass *klass = obj->klass;
    if (klass->singletonKlass != NULL) {
        klass = klass->singletonKlass;
    }
    Value key = OBJ_VAL(propName);
    while (klass) {
        if (tableGet(&klass->getters, key, ret)) {
            return true;
        }
        klass = klass->superclass;
    }
    return false;
}

// FIXME: use v_includedMods
static bool lookupSetter(ObjInstance *obj, ObjString *propName, Value *ret) {
    ObjClass *klass = obj->klass;
    if (klass->singletonKlass != NULL) {
        klass = klass->singletonKlass;
    }
    Value key = OBJ_VAL(propName);
    while (klass) {
        if (tableGet(&klass->setters, key, ret)) {
            return true;
        }
        klass = klass->superclass;
    }
    return false;
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

static InterpretResult vm_run(bool);

static Value propertyGet(ObjInstance *obj, ObjString *propName) {
    Value ret;
    if (tableGet(&obj->fields, OBJ_VAL(propName), &ret)) {
        return ret;
    } else if (lookupGetter(obj, propName, &ret)) {
        VM_DEBUG("getter found");
        callVMMethod(obj, ret, 0, NULL);
        if (vm.hadError) {
            return NIL_VAL;
        } else {
            return pop();
        }
    } else if (lookupMethod(obj, obj->klass, propName, &ret, true)) {
        ObjBoundMethod *bmethod = newBoundMethod(obj, AS_OBJ(ret));
        return OBJ_VAL(bmethod);
    } else {
        return NIL_VAL;
    }
}

static void propertySet(ObjInstance *obj, ObjString *propName, Value rval) {
    Value setterMethod;
    if (lookupSetter(obj, propName, &setterMethod)) {
        VM_DEBUG("setter found");
        callVMMethod(obj, setterMethod, 1, &rval);
        if (!vm.hadError) pop();
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
        VM_DEBUG("defining method '%s' in class '%s'", name->chars, klassName);
        ASSERT(tableSet(&klass->methods, OBJ_VAL(name), method));
    } else {
        ObjModule *mod = AS_MODULE(classOrMod);
        const char *modName = mod->name ? mod->name->chars : "(anon)";
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
    ASSERT(IS_CLASS(classOrMod));
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
    if (vm.hadError) {
        return NIL_VAL;
    } else {
        return peek(0);
    }
}


static void popFrame(void) {
    DBG_ASSERT(vm.inited);
    ASSERT(EC->frameCount >= 1);
    VM_DEBUG("popping callframe (%s)", getFrame()->isCCall ? "native" : "non-native");
    EC->frameCount--;
    inCCall = getFrame()->isCCall;
    ASSERT_VALID_STACK();
}

static CallFrame *pushFrame() {
    DBG_ASSERT(vm.inited);
    if (EC->frameCount >= FRAMES_MAX) {
        throwErrorFmt(lxErrClass, "Stackoverflow, max number of call frames (%d)", FRAMES_MAX);
        return NULL;
    }
    CallFrame *frame = &EC->frames[EC->frameCount++];
    frame->callLine = curLine;
    /*Value curFile;*/
    ASSERT(vm.fileString);
    frame->file = EC->filename;
    return frame;
}

static void pushNativeFrame(ObjNative *native) {
    DBG_ASSERT(vm.inited);
    ASSERT(native);
    VM_DEBUG("Pushing native callframe for %s", native->name->chars);
    if (EC->frameCount == FRAMES_MAX) {
        diePrintBacktrace("Stack overflow.");
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
    inCCall = true;
}

// sets up VM/C call jumpbuf if not set
static void captureNativeError(void) {
    if (!inCCall) {
        VM_DEBUG("%s", "Setting VM/C error jump buffer");
        int jumpRes = setjmp(CCallJumpBuf);
        if (jumpRes == 0) { // jump is set, prepared to enter C land
            return;
        } else { // C call longjmped here from throwError()
            ASSERT(getFrame()->isCCall);
            ASSERT(inCCall);
            ASSERT(cCallThrew);
            inCCall = false;
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
// argCount of 0. If the callable is a class, this function creates the
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
            vec_clear(&vm.stackObjects);
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
        UNREACHABLE("bug");
    }

    if (EC->frameCount >= FRAMES_MAX) {
        diePrintBacktrace("Stack overflow.");
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
    bool oldRunUntilReturn = runUntilReturn;
    runUntilReturn = true;
    vm_run(false); // actually run the function until return
    runUntilReturn = oldRunUntilReturn;
    return true;
}


/**
 * see doCallCallable
 * argCount does NOT include instance if `isMethod` is true
 */
bool callCallable(Value callable, int argCount, bool isMethod, CallInfo *info) {
    DBG_ASSERT(vm.inited);
    int lenBefore = vm.stackObjects.length;
    bool ret = doCallCallable(callable, argCount, isMethod, info);
    int lenAfter = vm.stackObjects.length;

    // allow collection of stack-created objects if they're not rooted now
    for (int i = lenBefore; i < lenAfter; i++) {
        (void)vec_pop(&vm.stackObjects);
    }

    return ret;
}

/**
 * When thrown (OP_THROW), find any surrounding try { } catch { } block with
 * the proper class.
 */
static bool findThrowJumpLoc(ObjClass *klass, uint8_t **ipOut, CatchTable **rowFound) {
    CatchTable *tbl = currentChunk()->catchTbl;
    CatchTable *row = tbl;
    int currentIpOff = (int)(getFrame()->ip - currentChunk()->code);
    bool poppedEC;
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

void throwError(Value self) {
    VM_DEBUG("throwing error");
    ASSERT(vm.inited);
    ASSERT(IS_INSTANCE(self));
    vm.lastErrorThrown = self;
    if (IS_NIL(getProp(self, internedString("backtrace", 9)))) {
        setBacktrace(self);
    }
    if (inCCall) {
        ASSERT(!cCallThrew);
        cCallThrew = true;
        longjmp(CCallJumpBuf, 1);
        UNREACHABLE("after longjmp");
    }
    // error from VM
    ObjInstance *obj = AS_INSTANCE(self);
    ObjClass *klass = obj->klass;
    CatchTable *catchRow;
    uint8_t *ipNew = NULL;
    if (findThrowJumpLoc(klass, &ipNew, &catchRow)) {
        ASSERT(ipNew);
        ASSERT(catchRow);
        catchRow->lastThrownValue = self;
        getFrame()->ip = ipNew;
        return; // frames were popped by `findThrowJumpLoc`
    } else {
        showUncaughtError(vm.lastErrorThrown);
    }
}

void throwErrorFmt(ObjClass *klass, const char *format, ...) {
    char sbuf[250] = {'\0'};
    va_list args;
    va_start(args, format);
    vsnprintf(sbuf, 250, format, args);
    va_end(args);
    size_t len = strlen(sbuf);
    char *cbuf = ALLOCATE(char, len+1);
    strncpy(cbuf, sbuf, len);
    cbuf[len] = '\0';
    ObjString *buf = takeString(cbuf, len);
    hideFromGC((Obj*)buf);
    Value msg = newStringInstance(buf);
    Value err = newError(klass, msg);
    vm.lastErrorThrown = err;
    unhideFromGC((Obj*)buf);
    throwError(err);
}

// FIXME: only prints VM operand stack for current execution context (EC)
void printVMStack(FILE *f) {
    if (EC->stackTop == EC->stack && vm.v_ecs.length == 1) {
        fprintf(f, "[DEBUG]: Stack: empty\n");
        return;
    }
    VMExecContext *ec = NULL; int i = 0;
    int numCallFrames = VMNumCallFrames();
    int numStackFrames = VMNumStackFrames();
    fprintf(f, "[DEBUG]: Stack (%d stack frames, %d call frames):\n", numStackFrames, numCallFrames);
    // print VM stack values from bottom of stack to top
    fprintf(f, "[DEBUG]: ");
    int callFrameIdx = 0;
    vec_foreach(&vm.v_ecs, ec, i) {
        for (Value *slot = ec->stack; slot < ec->stackTop; slot++) {
            if (IS_OBJ(*slot) && (AS_OBJ(*slot)->type <= OBJ_T_NONE)) {
                fprintf(stderr, "[DEBUG]: Broken object pointer: %p\n", AS_OBJ(*slot));
                ASSERT(0);
            }
            if (ec->frames[callFrameIdx].slots == slot) {
                fprintf(f, "(CF %d)", callFrameIdx+1);
                callFrameIdx++;
            }
            fprintf(f, "[ ");
            printValue(f, *slot, false);
            fprintf(f, " ]");
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
static InterpretResult vm_run(bool doResetStack) {
    if (!rootVMLoopJumpBufSet) {
        int jumpRes = setjmp(rootVMLoopJumpBuf);
        rootVMLoopJumpBufSet = true;
        if (jumpRes == 0) { // jump is set
        } else { // longjmp here from OP_LEAVE
            return INTERPRET_OK;
        }
    }
    if (CLOX_OPTION_T(parseOnly) || CLOX_OPTION_T(compileOnly)) {
        return INTERPRET_OK;
    }
#define READ_BYTE() (*getFrame()->ip++)
#define READ_CONSTANT() (currentChunk()->constants.values[READ_BYTE()])
#define BINARY_OP(op, opcode) \
    do { \
      Value b = pop(); \
      Value a = pop(); \
      if (IS_NUMBER(a) && IS_NUMBER(b)) {\
          if (opcode == OP_DIVIDE && AS_NUMBER(b) == 0.00) {\
              throwErrorFmt(lxErrClass, "Can't divide by 0");\
              break;\
          }\
          push(NUMBER_VAL(AS_NUMBER(a) op AS_NUMBER(b))); \
      } else if (opcode == OP_ADD && (IS_STRING(a) && IS_STRING(b))) {\
          ObjString *str = dupString(AS_STRING(a));\
          pushString(str, AS_STRING(b));\
          push(OBJ_VAL(str));\
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
              break;\
          }\
          callCallable(OBJ_VAL(callable), 1, true, NULL);\
      } else {\
          throwErrorFmt(lxTypeErrClass, "binary operation type error, lhs=%s, rhs=%s", typeOfVal(a), typeOfVal(b));\
          break;\
      }\
    } while (0)

  vmRunLvl++;
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

      Chunk *ch = currentChunk();
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
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      case OP_ADD:      BINARY_OP(+,OP_ADD); break;
      case OP_SUBTRACT: BINARY_OP(-,OP_SUBTRACT); break;
      case OP_MULTIPLY: BINARY_OP(*,OP_MULTIPLY); break;
      case OP_DIVIDE:   BINARY_OP(/,OP_DIVIDE); break;
      case OP_NEGATE: {
        Value val = pop();
        if (!IS_NUMBER(val)) {
            throwErrorFmt(lxTypeErrClass, "Can only negate numbers, type=%s", typeOfVal(val));
            break;
        }
        push(NUMBER_VAL(-AS_NUMBER(val)));
        break;
      }
      case OP_LESS: {
        Value rhs = pop(); // rhs
        Value lhs = pop(); // lhs
        if (!canCmpValues(lhs, rhs, instruction)) {
            throwErrorFmt(lxTypeErrClass,
                "Can only compare numbers and strings with '<', lhs=%s, rhs=%s",
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
                "Can only compare numbers and strings with '>', lhs=%s, rhs=%s",
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
                  "Can only compare numbers and strings with '>=', lhs=%s, rhs=%s",
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
                  "Can only compare numbers and strings with '<=', lhs=%s, rhs=%s",
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
              break;
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
            break;
        }
        break;
      }
      case OP_SET_GLOBAL: {
        Value val = peek(0);
        Value varName = READ_CONSTANT();
        char *name = AS_CSTRING(varName);
        if (isUnredefinableGlobal(name)) {
            throwErrorFmt(lxNameErrClass, "Can't redefine global variable '%s'", name);
            break;
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
          push(BOOL_VAL(isTruthy(lhs) && isTruthy(rhs)));
          break;
      }
      case OP_OR: {
          Value rhs = pop();
          Value lhs = pop();
          push(BOOL_VAL(isTruthy(lhs) || isTruthy(rhs)));
          break;
      }
      case OP_POP: {
          pop();
          break;
      }
      case OP_SET_LOCAL: {
          uint8_t slot = READ_BYTE();
          ASSERT(slot >= 0);
          getFrame()->slots[slot] = peek(0); // locals are popped at end of scope by VM
          break;
      }
      case OP_GET_LOCAL: {
          uint8_t slot = READ_BYTE();
          ASSERT(slot >= 0);
          push(getFrame()->slots[slot]);
          break;
      }
      case OP_GET_UPVALUE: {
          uint8_t slot = READ_BYTE();
          push(*getFrame()->closure->upvalues[slot]->value);
          break;
      }
      case OP_SET_UPVALUE: {
          uint8_t slot = READ_BYTE();
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
          }
          Value callableVal = peek(numArgs);
          if (!isCallable(callableVal)) {
              for (int i = 0; i < numArgs; i++) {
                  pop();
              }
              throwErrorFmt(lxTypeErrClass, "Tried to call uncallable object (type=%s)", typeOfVal(callableVal));
              break;
          }
          Value callInfoVal = READ_CONSTANT();
          CallInfo *callInfo = internalGetData(AS_INTERNAL(callInfoVal));
          // ex: String("hi"), "hi" already evaluates to a string instance, so we just
          // return that.
          if (numArgs == 1 && strcmp(tokStr(&callInfo->nameTok), "String") == 0) {
              Value strVal = pop();
              pop();
              push(strVal);
              lastSplatNumArgs = -1;
              break;
          }
          callCallable(callableVal, numArgs, false, callInfo);
          if (vm.hadError) {
              vmRunLvl--;
              return INTERPRET_RUNTIME_ERROR;
          }
          ASSERT_VALID_STACK();
          lastSplatNumArgs = -1;
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
          }
          Value instanceVal = peek(numArgs);
          if (IS_INSTANCE(instanceVal)) {
              ObjInstance *inst = AS_INSTANCE(instanceVal);
              Obj *callable = instanceFindMethod(inst, mname);
              if (!callable) {
                  ObjString *className = inst->klass->name;
                  const char *classStr = className->chars ? className->chars : "(anon)";
                  throwErrorFmt(lxErrClass, "instance method '%s#%s' not found", classStr, mname->chars);
                  lastSplatNumArgs = -1;
                  break;
              }
              setThis(numArgs);
              callCallable(OBJ_VAL(callable), numArgs, true, callInfo);
          } else if (IS_CLASS(instanceVal)) {
              ObjClass *klass = AS_CLASS(instanceVal);
              Obj *callable = classFindStaticMethod(klass, mname);
              if (!callable) {
                  ObjString *className = klass->name;
                  const char *classStr = className->chars ? className->chars : "(anon)";
                  throwErrorFmt(lxErrClass, "class method '%s.%s' not found", classStr, mname->chars);
                  lastSplatNumArgs = -1;
                  break;
              }
              EC->stackTop[-numArgs-1] = instanceVal;
              setThis(numArgs);
              callCallable(OBJ_VAL(callable), numArgs, true, callInfo);
          }
          // TODO: static module methods
          if (vm.hadError) {
              vmRunLvl--;
              return INTERPRET_RUNTIME_ERROR;
          }
          ASSERT_VALID_STACK();
          lastSplatNumArgs = -1;
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
              throwErrorFmt(lxTypeErrClass, "%s", "splatted expression must evaluate to an array");
              break;
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
              diePrintBacktrace("Could not find method"); // FIXME
              vmRunLvl--;
              return INTERPRET_RUNTIME_ERROR;
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
          if (runUntilReturn) {
              vmRunLvl--;
              return INTERPRET_OK;
          } else {
              ASSERT(0);
          }
          break;
      }
      case OP_CLASS: { // add or re-open class
          Value className = READ_CONSTANT();
          Value existingClass;
          // FIXME: not perfect, if class is declared non-globally this won't
          // detect it. Maybe a new op-code is needed for class re-opening.
          if (tableGet(&vm.globals, className, &existingClass) && IS_CLASS(existingClass)) {
              push(existingClass);
              break;
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
          if (tableGet(&vm.globals, modName, &existingMod) && IS_MODULE(existingMod)) {
              push(existingMod);
              break;
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
              break;
          }
          // FIXME: not perfect, if class is declared non-globally this won't detect it.
          Value existingClass;
          if (tableGet(&vm.globals, className, &existingClass) && IS_CLASS(existingClass)) {
              throwErrorFmt(lxNameErrClass, "Class %s already exists", AS_CSTRING(className));
              break;
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
                  throwErrorFmt(lxTypeErrClass, "expression given to 'in' statement must evaluate to a class/module/instance");
                  break;
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
              break;
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
              break;
          }
          propertySet(AS_INSTANCE(instance), propStr, rval); // TODO: check frozenness of object
          pop(); // leave rval on stack
          pop();
          push(rval);
          break;
      }
      case OP_INDEX_GET: {
          Value lval = peek(1); // ex: Array object
          ASSERT(IS_INSTANCE(lval)); // TODO: handle error, and allow classes/modules
          ObjInstance *instance = AS_INSTANCE(lval);
          Obj *method = instanceFindMethod(instance, internedString("indexGet", 8));
          ASSERT(method); // FIXME: handle method not found
          callCallable(OBJ_VAL(method), 1, true, NULL);
          break;
      }
      case OP_INDEX_SET: {
          Value lval = peek(2);
          ASSERT(IS_INSTANCE(lval)); // TODO: handle error, throw TypeError, allow classes/modules
          ObjInstance *instance = AS_INSTANCE(lval);
          Obj *method = instanceFindMethod(instance, internedString("indexSet", 8));
          ASSERT(method); // FIXME: handle method not found
          callCallable(OBJ_VAL(method), 2, true, NULL);
          break;
      }
      case OP_THROW: {
          Value throwable = pop();
          if (!isThrowable(throwable)) {
              throwErrorFmt(lxTypeErrClass, "Tried to throw unthrowable value, must throw an instance");
              break;
          }
          throwError(throwable);
          break;
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
          ASSERT(isThrowable(tblRow->lastThrownValue));
          push(tblRow->lastThrownValue);
          break;
      }
      case OP_STRING: {
          Value strLit = READ_CONSTANT();
          ASSERT(IS_STRING(strLit));
          uint8_t isStatic = READ_BYTE();
          push(OBJ_VAL(lxStringClass));
          push(strLit);
          bool ret = callCallable(peek(1), 1, false, NULL);
          ASSERT(ret); // the string instance is pushed to top of stack
          if (isStatic == 1) {
              objFreeze(AS_OBJ(peek(0)));
          }
          break;
      }
      // exit interpreter
      case OP_LEAVE: {
          vm.exited = true;
          if (doResetStack) resetStack();
          vmRunLvl--;
          if (vmRunLvl > 0) {
              longjmp(rootVMLoopJumpBuf, 1);
          }
          return INTERPRET_OK;
      }
      default:
          diePrintBacktrace("Unknown opcode instruction: %s (%d)", opName(instruction), instruction);
          vmRunLvl--;
          return INTERPRET_RUNTIME_ERROR;
    }
  }

  vmRunLvl--;
  return INTERPRET_RUNTIME_ERROR;
#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

static void setupPerScriptROGlobals(char *filename) {
    ObjString *file = copyString(filename, strlen(filename));
    tableSet(&EC->roGlobals, OBJ_VAL(vm.fileString), OBJ_VAL(file));

    if (filename[0] == pathSeparator) {
        char *lastSep = rindex(filename, pathSeparator);
        int len = lastSep - filename;
        ObjString *dir = copyString(filename, len);
        tableSet(&EC->roGlobals, OBJ_VAL(vm.dirString), OBJ_VAL(dir));
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

    InterpretResult result = vm_run(true);
    return result;
}

InterpretResult loadScript(Chunk *chunk, char *filename) {
    ASSERT(chunk);
    CallFrame *oldFrame = getFrame();
    push_EC();
    VMExecContext *ectx = EC;
    EC->filename = copyString(filename, strlen(filename));
    VM_DEBUG("%s", "Pushing initial callframe");
    CallFrame *frame = pushFrame();
    frame->start = 0;
    frame->ip = chunk->code;
    frame->slots = EC->stackTop-1;
    ObjFunction *func = newFunction(chunk, NULL);
    hideFromGC((Obj*)func);
    frame->closure = newClosure(func);
    unhideFromGC((Obj*)func);
    frame->isCCall = false;
    frame->nativeFunc = NULL;

    setupPerScriptROGlobals(filename);

    InterpretResult result = vm_run(false);
    // `EC != ectx` if an error occured in the script, and propagated out
    // due to being caught in a calling script or never being caught.
    if (EC == ectx) pop_EC();
    ASSERT(oldFrame == getFrame());
    return result;
}

Value VMEval(const char *src, const char *filename, int lineno) {
    CallFrame *oldFrame = getFrame();
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int oldOpts = compilerOpts.noRemoveUnusedExpressions;
    compilerOpts.noRemoveUnusedExpressions = true;
    push_EC();
    VMExecContext *ectx = EC;
    resetStack();
    int compile_res = compile_src(src, &chunk, &err);
    compilerOpts.noRemoveUnusedExpressions = oldOpts;

    if (compile_res != 0) {
        // TODO: throw syntax error
        pop_EC();
        ASSERT(getFrame() == oldFrame);
        freeChunk(&chunk);
        return BOOL_VAL(false);
    }
    EC->filename = copyString(filename, strlen(filename));
    VM_DEBUG("%s", "Pushing initial callframe");
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

    InterpretResult result = vm_run(false);
    if (result != INTERPRET_OK) {
        vm.hadError = true;
    }
    VM_DEBUG("eval finished");
    // `EC != ectx` if an error occured in the eval, and propagated out
    // due to being caught in a surrounding context or never being caught.
    if (EC == ectx) pop_EC();
    ASSERT(getFrame() == oldFrame);
    return BOOL_VAL(result == INTERPRET_OK);
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

NORETURN void stopVM(int status) {
    freeVM();
    exit(status);
}
