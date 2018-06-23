#include <stdio.h>
#include "common.h"
#include "vm.h"
#include "debug.h"
#include "options.h"
#include "runtime.h"
#include "memory.h"
#include <setjmp.h>

VM vm;

#ifndef NDEBUG
#define VM_DEBUG(...) vm_debug(__VA_ARGS__)
#else
#define VM_DEBUG(...) (void(0))
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

char *unredefinableGlobals[] = {
    "Object",
    "Array",
    "Error",
    "Map",
    "clock",
    "typeof",
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

void defineNativeFunctions() {
    ObjString *clockName = copyString("clock", 5);
    ObjNative *clockFn = newNative(clockName, runtimeNativeClock);
    tableSet(&vm.globals, OBJ_VAL(clockName), OBJ_VAL(clockFn));

    ObjString *typeofName = copyString("typeof", 6);
    ObjNative *typeofFn = newNative(typeofName, runtimeNativeTypeof);
    tableSet(&vm.globals, OBJ_VAL(typeofName), OBJ_VAL(typeofFn));
}

// Builtin classes:
ObjClass *lxObjClass;
ObjClass *lxAryClass;
ObjClass *lxMapClass;
ObjClass *lxErrClass;
ObjClass *lxArgErrClass;

void defineNativeClasses() {
    // class Object
    ObjString *objClassName = copyString("Object", 6);
    ObjClass *objClass = newClass(objClassName, NULL);
    tableSet(&vm.globals, OBJ_VAL(objClassName), OBJ_VAL(objClass));
    lxObjClass = objClass;

    // class Array
    ObjString *arrayClassName = copyString("Array", 5);
    ObjClass *arrayClass = newClass(arrayClassName, objClass);
    tableSet(&vm.globals, OBJ_VAL(arrayClassName), OBJ_VAL(arrayClass));
    lxAryClass = arrayClass;

    ObjNative *aryInitNat = newNative(copyString("init", 4), lxArrayInit);
    tableSet(&arrayClass->methods, OBJ_VAL(copyString("init", 4)), OBJ_VAL(aryInitNat));

    ObjNative *aryPushNat = newNative(copyString("push", 4), lxArrayPush);
    tableSet(&arrayClass->methods, OBJ_VAL(copyString("push", 4)), OBJ_VAL(aryPushNat));

    ObjNative *aryIdxGetNat = newNative(copyString("indexGet", 8), lxArrayIndexGet);
    tableSet(&arrayClass->methods, OBJ_VAL(copyString("indexGet", 8)), OBJ_VAL(aryIdxGetNat));

    ObjNative *aryIdxSetNat = newNative(copyString("indexSet", 8), lxArrayIndexSet);
    tableSet(&arrayClass->methods, OBJ_VAL(copyString("indexSet", 8)), OBJ_VAL(aryIdxSetNat));

    ObjNative *aryToStringNat = newNative(copyString("toString", 8), lxArrayToString);
    tableSet(&arrayClass->methods, OBJ_VAL(copyString("toString", 8)), OBJ_VAL(aryToStringNat));

    // class Map
    ObjString *mapClassName = copyString("Map", 3);
    ObjClass *mapClass = newClass(mapClassName, objClass);
    tableSet(&vm.globals, OBJ_VAL(mapClassName), OBJ_VAL(mapClass));
    lxMapClass = mapClass;

    ObjNative *mapInitNat = newNative(copyString("init", 4), lxMapInit);
    tableSet(&mapClass->methods, OBJ_VAL(copyString("init", 4)), OBJ_VAL(mapInitNat));

    ObjNative *mapIdxGetNat = newNative(copyString("indexGet", 8), lxMapIndexGet);
    tableSet(&mapClass->methods, OBJ_VAL(copyString("indexGet", 8)), OBJ_VAL(mapIdxGetNat));

    ObjNative *mapIdxSetNat = newNative(copyString("indexSet", 8), lxMapIndexSet);
    tableSet(&mapClass->methods, OBJ_VAL(copyString("indexSet", 8)), OBJ_VAL(mapIdxSetNat));

    ObjNative *mapKeysNat = newNative(copyString("keys", 4), lxMapKeys);
    tableSet(&mapClass->methods, OBJ_VAL(copyString("keys", 4)), OBJ_VAL(mapKeysNat));

    ObjNative *mapValuesNat = newNative(copyString("values", 6), lxMapValues);
    tableSet(&mapClass->methods, OBJ_VAL(copyString("values", 6)), OBJ_VAL(mapValuesNat));

    ObjNative *mapToStringNat = newNative(copyString("toString", 8), lxMapToString);
    tableSet(&mapClass->methods, OBJ_VAL(copyString("toString", 8)), OBJ_VAL(mapToStringNat));

    // class Error
    ObjString *errClassName = copyString("Error", 5);
    ObjClass *errClass = newClass(errClassName, objClass);
    tableSet(&vm.globals, OBJ_VAL(errClassName), OBJ_VAL(errClass));
    lxErrClass = errClass;

    ObjNative *errInitNat = newNative(copyString("init", 4), lxErrInit);
    tableSet(&errClass->methods, OBJ_VAL(copyString("init", 4)), OBJ_VAL(errInitNat));

    ObjString *argErrClassName = copyString("ArgumentError", 13);
    ObjClass *argErrClass = newClass(argErrClassName, errClass);
    tableSet(&vm.globals, OBJ_VAL(argErrClassName), OBJ_VAL(argErrClass));
    lxArgErrClass = argErrClass;
}


static jmp_buf CCallJumpBuf;
static bool inCCall = false;
static bool cCallThrew = false;
static bool returnedFromNativeErr = false;

void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
}

void initVM() {
    turnGCOff();
    resetStack();
    vm.objects = NULL;

    vm.bytesAllocated = 0;
    vm.nextGCThreshhold = 100;
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;
    vm.openUpvalues = NULL;

    vm.lastValue = NULL;
    vm.lastErrorThrown = NIL_VAL;
    vm.hadError = false;
    initTable(&vm.globals);
    initTable(&vm.strings);
    vm.initString = copyString("init", 4);
    defineNativeFunctions();
    defineNativeClasses();
    vec_init(&vm.hiddenObjs);
    vec_init(&vm.stackObjects);

    inCCall = false;
    cCallThrew = false;
    returnedFromNativeErr = false;
    memset(&CCallJumpBuf, 0, sizeof(CCallJumpBuf));

    turnGCOn();
    vm.inited = true;
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    vm.initString = NULL;
    vm.hadError = false;
    vm.printBuf = NULL;
    vm.lastValue = NULL;
    vm.objects = NULL;
    vm.grayStack = NULL;
    vm.openUpvalues = NULL;
    vec_deinit(&vm.hiddenObjs);
    vec_deinit(&vm.stackObjects);

    inCCall = false;
    cCallThrew = false;
    returnedFromNativeErr = false;
    memset(&CCallJumpBuf, 0, sizeof(CCallJumpBuf));

    freeObjects();
    vm.inited = false;
}

int VMNumStackFrames() {
    return vm.stackTop - vm.stack;
}

#define ASSERT_VALID_STACK(...) ASSERT(vm.stackTop >= vm.stack)

static bool isOpStackEmpty() {
    ASSERT_VALID_STACK();
    return vm.stackTop == vm.stack;
}

void push(Value value) {
    ASSERT_VALID_STACK();
    if (IS_OBJ(value)) {
        ASSERT(AS_OBJ(value)->type != OBJ_T_NONE);
    }
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    ASSERT(vm.stackTop > vm.stack);
    vm.stackTop--;
    vm.lastValue = vm.stackTop;
    return *vm.lastValue;
}

Value peek(unsigned n) {
    ASSERT((vm.stackTop-n) > vm.stack);
    return *(vm.stackTop-1-n);
}

Value *getLastValue() {
    if (isOpStackEmpty()) {
        return vm.lastValue;
    } else {
        return vm.stackTop-1;
    }
}

static Value nilValue() {
    return NIL_VAL;
}

static Value trueValue() {
    return BOOL_VAL(true);
}

static Value falseValue() {
    return BOOL_VAL(false);
}

static bool isTruthy(Value val) {
    switch (val.type) {
    case VAL_T_NIL: return false;
    case VAL_T_BOOL: return AS_BOOL(val);
    default:
        // all other values are truthy
        return true;

    }
}

static bool canCmpValues(Value lhs, Value rhs) {
    return IS_NUMBER(lhs) && IS_NUMBER(rhs);
}

static int cmpValues(Value lhs, Value rhs) {
    if (lhs.type == VAL_T_NUMBER && rhs.type == VAL_T_NUMBER) {
        double numA = AS_NUMBER(lhs);
        double numB = AS_NUMBER(rhs);
        if (numA == numB) {
            return 0;
        } else if (numA < numB) {
            return -1;
        } else {
            return 1;
        }
    }
    // TODO: error out
    return -2;
}

static inline CallFrame *getFrame() {
    ASSERT(vm.frameCount >= 1);
    return &vm.frames[vm.frameCount-1];
}

static Chunk *currentChunk() {
    return &getFrame()->closure->function->chunk;
}

void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame *frame = &vm.frames[i];
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
    Value msg = getProp(err, copyString("message", 7));
    char *msgStr = NULL;
    if (!IS_NIL(msg)) {
        msgStr = AS_CSTRING(msg);
    }
    Value bt = getProp(err, copyString("backtrace", 9));
    ASSERT(!IS_NIL(bt));
    int btSz = ARRAY_SIZE(bt);
    fprintf(stderr, "Uncaught Error, class: '%s'\n", className);
    if (msgStr) {
        fprintf(stderr, "Message: '%s'\n", msgStr);
    } else {
        fprintf(stderr, "Message: none\n");
    }
    fprintf(stderr, "Backtrace:\n");
    for (int i = 0; i < btSz; i++) {
        fprintf(stderr, "%s", AS_CSTRING(ARRAY_GET(bt, i)));
    }

    vm.hadError = true;
    resetStack();
}

// every new error value, when thrown, gets its backtrace set first
void setBacktrace(Value err) {
    /*ASSERT(IS_AN_ERROR(err));*/
    Value ret = newArray();
    setProp(err, copyString("backtrace", 9), ret);
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame *frame = &vm.frames[i];
        ObjString *out = hiddenString("", 0);
        if (frame->isCCall) {
            ObjNative *nativeFunc = frame->nativeFunc;
            pushCStringFmt(out, "native function %s()\n",
                nativeFunc->name->chars);
        } else {
            ObjFunction *function = frame->closure->function;
            size_t instruction = frame->ip - function->chunk.code - 1;
            pushCStringFmt(out, "[line %03d] in ", function->chunk.lines[instruction]);
            if (function->name == NULL) {
                pushCString(out, "script\n", 7); // top-level
            } else {
                char *fnName = function->name ? function->name->chars : "(anon)";
                pushCStringFmt(out, "%s()\n", fnName);
            }
        }
        arrayPush(ret, OBJ_VAL(out));
        unhideFromGC((Obj*)out);
    }
}

static bool isCallable(Value val) {
    return IS_CLASS(val) || IS_NATIVE_FUNCTION(val) ||
        IS_BOUND_METHOD(val) || IS_CLOSURE(val);
}

static bool isThrowable(Value val) {
    return IS_INSTANCE(val) && !IS_STRING(val);
}

static Value propertyGet(ObjInstance *obj, ObjString *propName) {
    Value ret;
    if (tableGet(&obj->fields, OBJ_VAL(propName), &ret)) {
        return ret;
    } else if (tableGet(&obj->klass->methods, OBJ_VAL(propName), &ret)) {
        ASSERT(isCallable(ret));
        ObjBoundMethod *bmethod = newBoundMethod(obj, AS_OBJ(ret));
        return OBJ_VAL(bmethod);
    } else {
        return NIL_VAL;
    }
}

static void propertySet(ObjInstance *obj, ObjString *propName, Value rval) {
    tableSet(&obj->fields, OBJ_VAL(propName), rval);
}

static void defineMethod(ObjString *name) {
    Value method = peek(0); // function
    ASSERT(IS_CLOSURE(method));
    ASSERT(IS_CLASS(peek(1)));
    ObjClass *klass = AS_CLASS(peek(1));
    VM_DEBUG("defining method '%s'", name->chars);
    ASSERT(tableSet(&klass->methods, OBJ_VAL(name), method));
    pop();
}

// Call method on instance, args are NOT expected to be pushed on to stack by
// caller. `argCount` does not include the implicit instance argument.
Value callVMMethod(ObjInstance *instance, Value callable, int argCount, Value *args) {
    VM_DEBUG("Calling VM method");
    push(OBJ_VAL(instance));
    for (int i = 0; i < argCount; i++) {
        ASSERT(args);
        push(args[i]);
    }
    callCallable(callable, argCount, true); // pushes return value to stack
    if (argCount > 0) {
        Value ret = peek(0);
        pop();
        for (int i = 0; i < argCount; i++) {
            pop();
        }
        pop(); // pop instance
        push(ret);
        VM_DEBUG("/Calling VM method");
        return ret;
    } else {
        Value ret = pop();
        pop(); // pop instance
        push(ret);
        VM_DEBUG("/Calling VM method");
        return ret;
    }
}

static void pushNativeFrame(ObjNative *native) {
    ASSERT(native);
    VM_DEBUG("Pushing native callframe for %s", native->name->chars);
    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return;
    }
    CallFrame *prevFrame = getFrame();
    CallFrame *newFrame = &vm.frames[vm.frameCount++];
    newFrame->closure = prevFrame->closure;
    newFrame->ip = prevFrame->ip;
    newFrame->start = 0;
    newFrame->slots = prevFrame->slots;
    newFrame->isCCall = true;
    newFrame->nativeFunc = native;
    inCCall = true;
}

static void popFrame() {
    ASSERT(vm.frameCount >= 1);
    VM_DEBUG("popping callframe (%s)", getFrame()->isCCall ? "native" : "non-native");
    vm.frameCount--;
    inCCall = getFrame()->isCCall;
    ASSERT_VALID_STACK();
}


// sets up VM/C call jumpbuf if not set
static void captureNativeError(void) {
    if (!inCCall) {
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

static void checkFunctionArity(ObjFunction *func, int argCount) {
    int arity = func->arity;
    if (argCount != arity) {
        throwArgErrorFmt("Expected %d arguments but got %d.",
            arity, argCount);
    }
}

// Arguments are expected to be pushed on to stack by caller. Argcount
// includes the instance argument, ex: a method with no arguments will have an
// argCount of 1. If the callable is a class, this function creates the
// new instance and puts it in the proper spot in the stack. The return value
// pushed to the stack. The caller is responsible for popping the arguments.
static bool doCallCallable(Value callable, int argCount, bool isMethod) {
    ObjClosure *closure = NULL;
    Value instanceVal;
    if (IS_CLOSURE(callable)) {
        closure = AS_CLOSURE(callable);
    } else if (IS_CLASS(callable)) {
        ObjClass *klass = AS_CLASS(callable);
        ObjInstance *instance = newInstance(klass);
        instanceVal = OBJ_VAL(instance);
        vm.stackTop[-argCount - 1] = instanceVal; // first argument is instance
        // Call the initializer, if there is one.
        Value initializer;
        Obj *init = instanceFindMethod(instance, vm.initString);
        if (init) {
            initializer = OBJ_VAL(init);
            if (IS_NATIVE_FUNCTION(initializer)) {
                captureNativeError();
                VM_DEBUG("calling native initializer with %d args", argCount);
                ObjNative *nativeInit = AS_NATIVE_FUNCTION(initializer);
                ASSERT(nativeInit->function);
                pushNativeFrame(nativeInit);
                CallFrame *newFrame = getFrame();
                nativeInit->function(argCount+1, vm.stackTop-argCount-1);
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
                    popFrame();
                    push(OBJ_VAL(instance));
                    return true;
                }
            }
            ASSERT(IS_CLOSURE(initializer));
            closure = AS_CLOSURE(initializer);
        } else if (argCount > 0) {
            throwArgErrorFmt("Expected 0 arguments (Object#init) but got %d.", argCount);
            return false;
        } else {
            push(instanceVal);
            return true; // new instance is on the top of the stack
        }
    } else if (IS_BOUND_METHOD(callable)) {
        VM_DEBUG("calling bound method with %d args", argCount);
        ObjBoundMethod *bmethod = AS_BOUND_METHOD(callable);
        Obj *callable = bmethod->callable; // native function or user-defined function (ObjClosure)
        instanceVal = bmethod->receiver;
        vm.stackTop[-argCount - 1] = instanceVal;
        return doCallCallable(OBJ_VAL(callable), argCount, true);
    } else if (IS_NATIVE_FUNCTION(callable)) {
        VM_DEBUG("Calling native function with %d args", argCount);
        ObjNative *native = AS_NATIVE_FUNCTION(callable);
        if (isMethod) argCount++;
        captureNativeError();
        pushNativeFrame(native);
        CallFrame *newFrame = getFrame();
        Value val = native->function(argCount, vm.stackTop-argCount);
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
            popFrame();
            push(val);
        }
        return true;
    } else {
        UNREACHABLE("bug");
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    // non-native function/method call
    ASSERT(closure);
    checkFunctionArity(closure->function, argCount);

    int parentStart = getFrame()->ip - getFrame()->closure->function->chunk.code - 2;
    ASSERT(parentStart >= 0);
    // add frame
    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->start = parentStart;
    frame->isCCall = false;
    frame->nativeFunc = NULL;

    // +1 to include either the called function or the receiver.
    frame->slots = vm.stackTop - (argCount + 1);
    return true;
}


bool callCallable(Value callable, int argCount, bool isMethod) {
    int lenBefore = vm.stackObjects.length;
    bool ret = doCallCallable(callable, argCount, isMethod);
    int lenAfter = vm.stackObjects.length;

    // allow collection of stack-created objects if they're not rooted now
    for (int i = lenBefore; i < lenAfter; i++) {
        (void)vec_pop(&vm.stackObjects);
    }
    ASSERT(lenBefore == vm.stackObjects.length);

    return ret;
}

/**
 * When thrown (OP_THROW), find any surrounding try { } catch { } block with
 * the proper class
 */
static bool findThrowJumpLoc(ObjClass *klass, uint8_t **ipOut, CatchTable **rowFound) {
    CatchTable *tbl = currentChunk()->catchTbl;
    CatchTable *row = tbl;
    int currentIpOff = (int)(getFrame()->ip - currentChunk()->code);
    while (row || vm.frameCount > 1) {
        if (row == NULL) { // pop a call frame
            ASSERT(vm.frameCount > 1);
            currentIpOff = getFrame()->start;
            ASSERT(vm.stackTop > getFrame()->slots);
            vm.stackTop = getFrame()->slots;
            popFrame();
            row = currentChunk()->catchTbl;
            continue;
        }
        Value klassFound;
        if (!tableGet(&vm.globals, row->catchVal, &klassFound)) {
            row = row->next;
            continue;
        }
        if (IS_SUBCLASS(klass, AS_CLASS(klassFound))) {
            if (currentIpOff > row->ifrom && currentIpOff <= row->ito) {
                // found target catch
                *ipOut = currentChunk()->code + row->itarget;
                *rowFound = row;
                return true;
            }
        }
        row = row->next;
    }

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
    ASSERT(vm.inited);
    vm.lastErrorThrown = self;
    if (IS_NIL(getProp(self, copyString("backtrace", 9)))) {
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

void throwArgErrorFmt(const char *format, ...) {
    char sbuf[250] = {'\0'};
    va_list args;
    va_start(args, format);
    vsnprintf(sbuf, 250, format, args);
    va_end(args);
    size_t len = strlen(sbuf);
    char *cbuf = ALLOCATE(char, len+1);
    strncpy(cbuf, sbuf, len);
    cbuf[len] = '\0';
    ObjString *buf = newString(cbuf, len);
    hideFromGC((Obj*)buf);
    Value err = newError(lxArgErrClass, buf);
    vm.lastErrorThrown = err;
    unhideFromGC((Obj*)buf);
    throwError(err);
}

void printVMStack(FILE *f) {
    if (vm.stackTop == vm.stack) {
        fprintf(f, "%s", "Stack: empty\n");
        return;
    }
    fprintf(f, "%s", "Stack:\n");
    // print VM stack values from bottom of stack to top
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
        if (IS_OBJ(*slot) && (AS_OBJ(*slot)->type <= OBJ_T_NONE)) {
            fprintf(stderr, "Broken object pointer: %p\n", AS_OBJ(*slot));
            ASSERT(0);
        }
        fprintf(f, "%s", "[ ");
        printValue(f, *slot, false);
        fprintf(f, "%s", " ]");
    }
    fprintf(f, "%s", "\n");
}

ObjUpvalue *captureUpvalue(Value *local) {
    if (vm.openUpvalues == NULL) {
        vm.openUpvalues = newUpvalue(local);
        return vm.openUpvalues;
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
  while (vm.openUpvalues != NULL &&
         vm.openUpvalues->value >= last) {
    ObjUpvalue *upvalue = vm.openUpvalues;

    // Move the value into the upvalue itself and point the upvalue to it.
    upvalue->closed = *upvalue->value;
    upvalue->value = &upvalue->closed;

    // Pop it off the open upvalue list.
    vm.openUpvalues = upvalue->next;
  }
}

/**
 * Run the VM's instructions.
 */
static InterpretResult run(void) {
    if (CLOX_OPTION_T(parseOnly) || CLOX_OPTION_T(compileOnly)) {
        return INTERPRET_OK;
    }
#define READ_BYTE() (*getFrame()->ip++)
#define READ_CONSTANT() (currentChunk()->constants.values[READ_BYTE()])
#define BINARY_OP(op) \
    do { \
      Value b = pop(); \
      Value a = pop(); \
      if (!IS_NUMBER(a) || !IS_NUMBER(b)) {\
        return INTERPRET_RUNTIME_ERROR;\
      }\
      push(NUMBER_VAL(AS_NUMBER(a) op AS_NUMBER(b))); \
    } while (0)

  for (;;) {
      if (vm.hadError) {
          return INTERPRET_RUNTIME_ERROR;
      }
      if (vm.stackTop < vm.stack) {
          ASSERT(0);
      }

#ifndef NDEBUG
    if (CLOX_OPTION_T(traceVMExecution)) {
        printVMStack(stderr);
        printDisassembledInstruction(currentChunk(), (int)(getFrame()->ip - currentChunk()->code), NULL);
    }
#endif

    uint8_t instruction = READ_BYTE();
    switch (instruction) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      // TODO: allow addition of strings
      case OP_ADD:      BINARY_OP(+); break;
      case OP_SUBTRACT: BINARY_OP(-); break;
      case OP_MULTIPLY: BINARY_OP(*); break;
      case OP_DIVIDE:   BINARY_OP(/); break;
      case OP_NEGATE: {
        Value val = pop();
        if (!IS_NUMBER(val)) {
            // FIXME: throw error
            runtimeError("Can only negate numbers");
            return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(val)));
        break;
      }
      case OP_LESS: {
        Value rhs = pop(); // rhs
        Value lhs = pop(); // lhs
        if (!canCmpValues(lhs, rhs)) {
            // FIXME: throw error
            runtimeError("Can only compare numbers");
            return INTERPRET_RUNTIME_ERROR;
        }
        if (cmpValues(lhs, rhs) == -1) {
            push(trueValue());
        } else {
            push(falseValue());
        }
        break;
      }
      case OP_GREATER: {
        Value rhs = pop(); // rhs
        Value lhs = pop(); // lhs
        if (!canCmpValues(lhs, rhs)) {
            runtimeError("Can only compare numbers");
            return INTERPRET_RUNTIME_ERROR;
        }
        if (cmpValues(lhs, rhs) == 1) {
            push(trueValue());
        } else {
            push(falseValue());
        }
        break;
      }
      case OP_PRINT: {
        Value val = pop();
        if (vm.printBuf) {
            ObjString *out = valueToString(val, hiddenString);
            pushCString(vm.printBuf, out->chars, strlen(out->chars));
            pushCString(vm.printBuf, "\n", 1);
            unhideFromGC((Obj*)out);
            freeObject((Obj*)out, true);
        } else {
            printValue(stdout, val, true);
            printf("\n");
        }
        break;
      }
      case OP_DEFINE_GLOBAL: {
        Value varName = READ_CONSTANT();
        char *name = AS_CSTRING(varName);
        if (isUnredefinableGlobal(name)) {
            runtimeError("Can't redeclare global variable '%s'", name);
            return INTERPRET_RUNTIME_ERROR;
        }
        Value val = peek(0);
        tableSet(&vm.globals, varName, val);
        pop();
        break;
      }
      case OP_GET_GLOBAL: {
        Value varName = READ_CONSTANT();
        Value val;
        if (tableGet(&vm.globals, varName, &val)) {
            push(val);
        } else {
            runtimeError("Undefined variable '%s'.", AS_STRING(varName)->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SET_GLOBAL: {
        Value val = peek(0);
        Value varName = READ_CONSTANT();
        char *name = AS_CSTRING(varName);
        if (isUnredefinableGlobal(name)) {
            runtimeError("Can't redefine global variable '%s'", name);
            return INTERPRET_RUNTIME_ERROR;
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
          getFrame()->slots[slot] = peek(0);
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
          closeUpvalues(vm.stackTop - 1);
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
              getFrame()->ip += ipOffset;
          }
          break;
      }
      case OP_JUMP_IF_FALSE_PEEK: {
          Value cond = peek(0);
          uint8_t ipOffset = READ_BYTE();
          if (!isTruthy(cond)) {
              ASSERT(ipOffset > 0);
              getFrame()->ip += ipOffset;
          }
          break;
      }
      case OP_JUMP_IF_TRUE_PEEK: {
          Value cond = peek(0);
          uint8_t ipOffset = READ_BYTE();
          if (isTruthy(cond)) {
              ASSERT(ipOffset > 0);
              getFrame()->ip += ipOffset;
          }
          break;
      }
      case OP_JUMP: {
          uint8_t ipOffset = READ_BYTE();
          ASSERT(ipOffset > 0);
          getFrame()->ip += ipOffset;
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
          Value callableVal = peek(numArgs);
          if (!isCallable(callableVal)) {
              runtimeError("Tried to call uncallable object (type=%s)", typeOfVal(callableVal));
              return INTERPRET_RUNTIME_ERROR;
          }
          callCallable(callableVal, numArgs, false);
          ASSERT_VALID_STACK();
          break;
      }
      // return from function/method
      case OP_RETURN: {
          Value result = pop();
          ASSERT(!getFrame()->isCCall);
          Value *newTop = getFrame()->slots;
          closeUpvalues(getFrame()->slots);
          popFrame();
          vm.stackTop = newTop;
          push(result);
          break;
      }
      case OP_CLASS: {
          Value className = READ_CONSTANT();
          Value objClassVal;
          ASSERT(tableGet(&vm.globals, OBJ_VAL(copyString("Object", 6)), &objClassVal));
          ASSERT(IS_CLASS(objClassVal));
          ObjClass *klass = newClass(AS_STRING(className), AS_CLASS(objClassVal));
          push(OBJ_VAL(klass));
          break;
      }
      case OP_SUBCLASS: {
          Value className = READ_CONSTANT();
          Value superclass =  pop();
          if (!IS_CLASS(superclass)) {
              runtimeError(
                  "Class %s tried to inherit from non-class",
                  AS_CSTRING(className)
              );
              return INTERPRET_RUNTIME_ERROR;
          }
          ObjClass *klass = newClass(
              AS_STRING(className),
              AS_CLASS(superclass)
          );
          push(OBJ_VAL(klass));
          break;
      }
      case OP_METHOD: {
          Value methodName = READ_CONSTANT();
          ObjString *methStr = AS_STRING(methodName);
          defineMethod(methStr);
          break;
      }
      case OP_PROP_GET: {
          Value propName = READ_CONSTANT();
          ObjString *propStr = AS_STRING(propName);
          ASSERT(propStr && propStr->chars);
          Value instance = peek(0);
          if (!IS_INSTANCE(instance)) {
              runtimeError("Tried to access property '%s' on non-instance (type: %s)", propStr->chars, typeOfVal(instance));
              return INTERPRET_RUNTIME_ERROR;
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
          if (!IS_INSTANCE(instance)) {
              runtimeError("Tried to set property '%s' on non-instance", propStr->chars);
              return INTERPRET_RUNTIME_ERROR;
          }
          propertySet(AS_INSTANCE(instance), propStr, rval);
          pop(); // leave rval on stack
          pop();
          push(rval);
          break;
      }
      case OP_CREATE_ARRAY: {
          Value numElsVal = pop();
          int numEls = (int)AS_NUMBER(numElsVal);
          ASSERT(numEls >= 0);
          callCallable(OBJ_VAL((Obj*)lxAryClass), numEls, false); // new array pushed to top of stack
          Value ret = peek(0);
          ASSERT(IS_T_ARRAY(ret));
          ret = pop();
          for (int i = 0; i < numEls; i++) {
              pop();
          }
          push(ret);
          break;
      }
      case OP_INDEX_GET: {
          Value lval = peek(1);
          ASSERT(IS_INSTANCE(lval)); // TODO: handle error
          ObjInstance *instance = AS_INSTANCE(lval);
          Obj *method = instanceFindMethod(instance, copyString("indexGet", 8));
          ASSERT(method); // TODO: handle error
          callCallable(OBJ_VAL(method), 1, true);
          Value ret = pop();
          pop(); pop();
          push(ret);
          break;
      }
      case OP_INDEX_SET: {
          Value lval = peek(2);
          ASSERT(IS_INSTANCE(lval)); // TODO: handle error
          ObjInstance *instance = AS_INSTANCE(lval);
          Obj *method = instanceFindMethod(instance, copyString("indexSet", 8));
          ASSERT(method); // TODO: handle error
          callCallable(OBJ_VAL(method), 2, true);
          Value ret = pop();
          pop(); pop(); pop();
          push(ret);
          break;
      }
      case OP_THROW: {
          Value throwable = pop();
          if (!isThrowable(throwable)) {
              runtimeError("Tried to throw unthrowable value, must throw an instance");
              return INTERPRET_RUNTIME_ERROR;
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
              // FIXME: throw typeerror
              fprintf(stderr, "Non-throwable found (BUG): %s\n", typeOfVal(tblRow->lastThrownValue));
          }
          ASSERT(isThrowable(tblRow->lastThrownValue));
          push(tblRow->lastThrownValue);
          break;
      }
      // exit interpreter
      case OP_LEAVE:
          resetStack();
          return INTERPRET_OK;
      default:
          runtimeError("Unknown opcode instruction: %s (%d)", opName(instruction), instruction);
          return INTERPRET_RUNTIME_ERROR;
    }
  }

  return INTERPRET_RUNTIME_ERROR;
#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult interpret(Chunk *chunk) {
    ASSERT(chunk);
    vm.frameCount = 0;
    // initialize top-level callframe (frameCount = 1)
    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->start = 0;
    frame->ip = chunk->code;
    frame->slots = vm.stack;
    ObjFunction *func = newFunction(chunk);
    hideFromGC((Obj*)func);
    frame->closure = newClosure(func);
    unhideFromGC((Obj*)func);
    frame->isCCall = false;
    frame->nativeFunc = NULL;

    InterpretResult result = run();
    return result;
}

void setPrintBuf(ObjString *buf) {
    vm.printBuf = buf;
}

void unsetPrintBuf(void) {
    vm.printBuf = NULL;
}
