#include <stdio.h>
#include "common.h"
#include "vm.h"
#include "debug.h"
#include "options.h"
#include "runtime.h"
#include "memory.h"

VM vm;

char *unredefinableGlobals[] = {
    "Object",
    "Array",
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

ObjClass *lxObjClass;
ObjClass *lxAryClass;
ObjClass *lxMapClass;

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
}

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

    vm.lastValue = NULL;
    vm.hadError = false;
    initTable(&vm.globals);
    initTable(&vm.strings);
    vm.initString = copyString("init", 4);
    defineNativeFunctions();
    defineNativeClasses();
    vec_init(&vm.hiddenObjs);
    vec_init(&vm.stackObjects);
    turnGCOn();
    vm.inited = true;
}

void freeVM() {
    turnGCOff();
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    vm.initString = NULL;
    vm.hadError = false;
    vm.printBuf = NULL;
    vm.lastValue = NULL;
    vm.objects = NULL;
    vm.grayStack = NULL;
    vec_deinit(&vm.hiddenObjs);
    vec_deinit(&vm.stackObjects);
    freeObjects();
    turnGCOn();
    vm.inited = false;
}

int VMNumStackFrames() {
    return vm.stackTop - vm.stack;
}

static bool isOpStackEmpty() {
    return vm.stackTop <= vm.stack;
}

void push(Value value) {
    ASSERT(vm.stackTop >= vm.stack);
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
    return &getFrame()->function->chunk;
}

// TODO: throw lox error using setjmp/jmpbuf to allow catches
void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame *frame = &vm.frames[i];
        ObjFunction *function = frame->function;
        // -1 because the IP is sitting on the next instruction to be executed.
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    vm.hadError = true;
    resetStack();
}

static bool isCallable(Value val) {
    return IS_FUNCTION(val) ||
        IS_CLASS(val) || IS_NATIVE_FUNCTION(val) ||
        IS_BOUND_METHOD(val);
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
    ASSERT(IS_FUNCTION(method));
    ASSERT(IS_CLASS(peek(1)));
    ObjClass *klass = AS_CLASS(peek(1));
    /*fprintf(stderr, "defining method '%s' (%d)\n", name->chars, (int)strlen(name->chars));*/
    ASSERT(tableSet(&klass->methods, OBJ_VAL(name), method));
    pop();
}

// fwd decl
static bool callCallable(Value callable, int argCount, bool isMethod);

// Call method on instance, args are NOT expected to be pushed on to stack by
// caller. `argCount` does not include the implicit instance argument.
Value callVMMethod(ObjInstance *instance, Value callable, int argCount, Value *args) {
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
        return ret;
    } else {
        Value ret = pop();
        pop(); // pop instance
        push(ret);
        return ret;
    }
}

// arguments are expected to be pushed on to stack by caller
static bool doCallCallable(Value callable, int argCount, bool isMethod) {
    ObjFunction *function = NULL;
    Value instanceVal;
    if (IS_FUNCTION(callable)) {
        function = AS_FUNCTION(callable);
        int arity = function->arity;
        if (argCount != arity) {
            runtimeError("Expected %d arguments but got %d.",
                arity, argCount);
            return false;
        }
    } else if (IS_CLASS(callable)) {
        ObjClass *klass = AS_CLASS(callable);
        ObjInstance *instance = newInstance(klass);
        instanceVal = OBJ_VAL(instance);
        vm.stackTop[-argCount - 1] = instanceVal; // first argument is instance
        // Call the initializer, if there is one.
        Value initializer;
        if (tableGet(&klass->methods, OBJ_VAL(vm.initString), &initializer)) {
            if (IS_NATIVE_FUNCTION(initializer)) {
                /*fprintf(stderr, "calling native initializer with %d args\n", argCount);*/
                ObjNative *nativeInit = AS_NATIVE_FUNCTION(initializer);
                ASSERT(nativeInit->function);
                nativeInit->function(argCount+1, vm.stackTop-argCount-1);
                push(OBJ_VAL(instance));
                return true;
            }
            ASSERT(IS_FUNCTION(initializer));
            function = AS_FUNCTION(initializer);
        } else if (argCount > 0) {
          runtimeError("Expected 0 arguments (default init) but got %d.", argCount);
          return false;
        } else {
            return true; // new instance is on the top of the stack
        }
    } else if (IS_BOUND_METHOD(callable)) {
        /*fprintf(stderr, "calling bound method with %d args\n", argCount);*/
        ObjBoundMethod *bmethod = AS_BOUND_METHOD(callable);
        Obj *callable = bmethod->callable; // native function or user-defined function
        instanceVal = bmethod->receiver;
        vm.stackTop[-argCount - 1] = instanceVal;
        return callCallable(OBJ_VAL(callable), argCount, true);
    } else if (IS_NATIVE_FUNCTION(callable)) {
        /*fprintf(stderr, "Calling native function with %d args\n", argCount);*/
        ObjNative *native = AS_NATIVE_FUNCTION(callable);
        if (isMethod) argCount++;
        Value val = native->function(argCount, vm.stackTop-argCount);
        push(val);
        return true;
    } else {
        ASSERT(0);
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    int parentStart = getFrame()->ip - getFrame()->function->chunk.code - 2;
    ASSERT(parentStart >= 0);
    fprintf(stderr, "setting new call frame to start=%d\n", parentStart);
    // add frame
    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->function = function;
    frame->ip = function->chunk.code;
    frame->start = parentStart;

    // +1 to include either the called function or the receiver.
    frame->slots = vm.stackTop - (argCount + 1);
    return true;
}


static bool callCallable(Value callable, int argCount, bool isMethod) {
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
    char *klassName = klass->name->chars;
    int currentIpOff = (int)(getFrame()->ip - currentChunk()->code);
    while (row || vm.frameCount > 1) {
        if (row == NULL) {
            ASSERT(vm.frameCount > 1);
            currentIpOff = getFrame()->start;
            vm.frameCount--;
            row = currentChunk()->catchTbl;
            continue;
        }
        if (strcmp(AS_CSTRING(row->catchVal), klassName) == 0) {
            if (currentIpOff > row->ifrom && currentIpOff <= row->ito) {
                // found target catch
                *ipOut = currentChunk()->code + row->itarget;
                *rowFound = row;
                /*currentIpOff = (int)(*ipOut - currentChunk()->code);*/
                /*fprintf(stderr, "new ip offset: %d\n", currentIpOff);*/
                fprintf(stderr, "found catch row\n");
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
          hideFromGC(AS_OBJ(callableVal));
          if (!isCallable(callableVal)) {
              runtimeError("Tried to call uncallable object (type=%s)", typeOfVal(callableVal));
              return INTERPRET_RUNTIME_ERROR;
          }
          callCallable(callableVal, numArgs, false);
          unhideFromGC(AS_OBJ(callableVal));
          break;
      }
      // return from function/method
      case OP_RETURN: {
          /*fprintf(stderr, "opcall in return\n");*/
          Value result = pop();
          vm.stackTop = getFrame()->slots;
          ASSERT(vm.frameCount > 0);
          vm.frameCount--;
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
          ASSERT(IS_ARRAY(ret));
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
          ObjInstance *obj = AS_INSTANCE(throwable);
          ObjClass *klass = obj->klass;
          uint8_t *ipNew = NULL;
          CatchTable *catchRow = NULL;
          if (findThrowJumpLoc(klass, &ipNew, &catchRow)) {
              ASSERT(ipNew);
              ASSERT(catchRow);
              catchRow->lastThrownValue = throwable;
              getFrame()->ip = ipNew;
          } else {
              runtimeError("Uncaught exception: %s", klass->name->chars);
              return INTERPRET_RUNTIME_ERROR;
          }
          break;
      }
      case OP_GET_THROWN: {
          Value catchTblIdx = READ_CONSTANT();
          ASSERT(IS_NUMBER(catchTblIdx));
          double idx = AS_NUMBER(catchTblIdx);
          CatchTable *tblRow = getCatchTableRow((int)idx);
          if (!isThrowable(tblRow->lastThrownValue)) { // bug
              fprintf(stderr, "Non-throwable found (BUG): %s\n", typeOfVal(tblRow->lastThrownValue));
          }
          ASSERT(isThrowable(tblRow->lastThrownValue));
          push(tblRow->lastThrownValue);
          break;
      }
      // exit interpreter
      case OP_LEAVE:
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
    // initialize top-level callframe
    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->start = 0;
    frame->ip = chunk->code;
    frame->slots = vm.stack;
    frame->function = newFunction(chunk);

    InterpretResult result = run();
    return result;
}

void setPrintBuf(ObjString *buf) {
    vm.printBuf = buf;
}

void unsetPrintBuf(void) {
    vm.printBuf = NULL;
}
