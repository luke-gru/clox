#include <stdio.h>
#include "common.h"
#include "vm.h"
#include "debug.h"
#include "options.h"
#include "runtime.h"

VM vm;

void defineNativeFunctions() {
    ObjString *clockName = copyString("clock", 5);
    ObjNative *clockFn = newNative(clockName, runtimeNativeClock);
    tableSet(&vm.globals, clockName, OBJ_VAL(clockFn));
}

void initVM() {
    vm.stackTop = vm.stack;
    vm.objects = NULL;
    vm.lastValue = NULL;
    vm.hadError = false;
    vm.frameCount = 0;
    initTable(&vm.globals);
    initTable(&vm.strings);
    vm.initString = copyString("init", 4);
    defineNativeFunctions();
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    // TODO: free object list and initString
    vm.objects = NULL;
    freeString(vm.initString);
    vm.hadError = false;
}

static bool isOpStackEmpty() {
    return vm.stackTop <= vm.stack;
}

void push(Value value) {
    ASSERT(vm.stackTop >= vm.stack);
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    ASSERT(vm.stackTop > vm.stack);
    vm.stackTop--;
    return *vm.stackTop;
}

Value peek(unsigned n) {
    ASSERT((vm.stackTop-n) > vm.stack);
    return *(vm.stackTop-1-n);
}

Value *getLastValue() {
    if (isOpStackEmpty()) {
        return NULL;
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
    case VAL_NIL: return false;
    case VAL_BOOL: return AS_BOOL(val);
    default:
        // all other values are truthy
        return true;

    }
}

static bool canCmpValues(Value lhs, Value rhs) {
    return IS_NUMBER(lhs) && IS_NUMBER(rhs);
}

static int cmpValues(Value lhs, Value rhs) {
    if (lhs.type == VAL_NUMBER && rhs.type == VAL_NUMBER) {
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

static void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
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

static const char *typeOfObj(Obj *obj) {
    switch (obj->type) {
    case OBJ_STRING:
        return "string";
    case OBJ_FUNCTION:
    case OBJ_NATIVE_FUNCTION:
        return "function";
    case OBJ_CLASS:
        return "class";
    case OBJ_BOUND_METHOD:
        return "method";
    case OBJ_INSTANCE:
        return "instance";
    default:
        ASSERT(0);
        return "unknown";
    }
}

static const char *typeOf(Value val) {
    if (IS_OBJ(val)) {
        return typeOfObj(AS_OBJ(val));
    } else {
        if (IS_BOOL(val)) return "bool";
        if (IS_NIL(val)) return "nil";
        if (IS_NUMBER(val)) return "number";
    }
    ASSERT(0);
    return "unknown!";
}

static Value propertyGet(ObjInstance *obj, ObjString *propName) {
    Value ret;
    if (tableGet(&obj->fields, propName, &ret)) {
        return ret;
    } else if (tableGet(&obj->klass->methods, propName, &ret)) {
        ASSERT(IS_FUNCTION(ret));
        ObjBoundMethod *bmethod = newBoundMethod(obj, AS_FUNCTION(ret));
        return OBJ_VAL(bmethod);
    } else {
        return NIL_VAL;
    }
}

static void propertySet(ObjInstance *obj, ObjString *propName, Value rval) {
    tableSet(&obj->fields, propName, rval);
}

static void defineMethod(ObjString *name) {
    Value method = peek(0); // function
    ASSERT(IS_FUNCTION(method));
    ASSERT(IS_CLASS(peek(1)));
    ObjClass *klass = AS_CLASS(peek(1));
    tableSet(&klass->methods, name, method);
    pop();
}

static bool callCallable(Value callable, int argCount) {
    ObjFunction *function = NULL;
    Value instanceVal;
    if (IS_FUNCTION(callable)) {
        function = AS_FUNCTION(callable);
        if (argCount != function->arity) {
            runtimeError("Expected %d arguments but got %d.",
                    function->arity, argCount);
            return false;
        }
    } else if (IS_CLASS(callable)) {
        ObjClass *klass = AS_CLASS(callable);
        ObjInstance *instance = newInstance(klass);
        instanceVal = OBJ_VAL(instance);
        vm.stackTop[-argCount - 1] = instanceVal;
        // Call the initializer, if there is one.
        Value initializer;
        if (tableGet(&klass->methods, vm.initString, &initializer)) {
            ASSERT(IS_FUNCTION(initializer));
            function = AS_FUNCTION(initializer);
            // TODO: check arity
        } else if (argCount != 0) {
          runtimeError("Expected 0 arguments but got %d.", argCount);
          return false;
        } else {
            return true; // new instance is on the top of the stack
        }
    } else if (IS_BOUND_METHOD(callable)) {
        ObjBoundMethod *bmethod = AS_BOUND_METHOD(callable);
        function = bmethod->method;
        instanceVal = bmethod->receiver;
        vm.stackTop[-argCount - 1] = instanceVal;
    } else if (IS_NATIVE_FUNCTION(callable)) {
        ObjNative *native = AS_NATIVE_FUNCTION(callable);
        Value val = native->function(argCount, vm.stackTop-argCount-1);
        push(val);
        return true;
    } else {
        ASSERT(0);
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }
    // add frame
    CallFrame *frame = &vm.frames[vm.frameCount++];
    ASSERT(getFrame() == frame);
    frame->function = function;
    frame->ip = function->chunk.code;

    // +1 to include either the called function or the receiver.
    frame->slots = vm.stackTop - (argCount + 1);
    return true;
}

static bool findThrowJumpLoc(ObjClass *klass, uint8_t **ipOut, CatchTable **rowFound) {
    CatchTable *tbl = currentChunk()->catchTbl;
    CatchTable *row = tbl;
    char *klassName = klass->name->chars;
    int currentIpOff = (int)(getFrame()->ip - currentChunk()->code);
    while (row) {
        if (strcmp(AS_CSTRING(row->catchVal), klassName) == 0) {
            if (currentIpOff > row->ifrom && currentIpOff <= row->ito) {
                // found target catch
                *ipOut = currentChunk()->code + row->itarget;
                *rowFound = row;
                currentIpOff = (int)(*ipOut - currentChunk()->code);
                fprintf(stderr, "new ip offset: %d\n", currentIpOff);
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
        printf("          ");
        // print VM stack values from bottom of stack to top
        for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
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
        printValue(val);
        printf("\n");
        break;
      }
      case OP_DEFINE_GLOBAL: {
        Value varName = READ_CONSTANT();
        Value val = pop();
        tableSet(&vm.globals, AS_STRING(varName), val);
        break;
      }
      case OP_GET_GLOBAL: {
        Value varName = READ_CONSTANT();
        Value val;
        memset(&val, 0, sizeof(Value));
        if (tableGet(&vm.globals, AS_STRING(varName), &val)) {
            push(val);
        } else {
            runtimeError("Undefined variable '%s'.", AS_STRING(varName)->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SET_GLOBAL: {
        Value val = pop();
        Value varName = READ_CONSTANT();
        tableSet(&vm.globals, AS_STRING(varName), val);
        push(val);
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
      case OP_JUMP: {
          uint8_t ipOffset = READ_BYTE();
          ASSERT(ipOffset > 0);
          getFrame()->ip += ipOffset;
          break;
      }
      case OP_LOOP: {
          uint8_t ipOffset = READ_BYTE();
          ASSERT(ipOffset > 0);
          /*fprintf(stderr, "loop offset: %d\n", ipOffset+1);*/
          // add 1 for the instruction we just read, and 1 to go 1 before the
          // instruction we want to execute next.
          getFrame()->ip -= (ipOffset+2);
          break;
      }
      case OP_CALL: {
          uint8_t numArgs = READ_BYTE();
          Value callableVal = peek(numArgs);
          if (!isCallable(callableVal)) {
              runtimeError("Tried to call uncallable object (type=%s)", typeOf(callableVal));
              return INTERPRET_RUNTIME_ERROR;
          }
          callCallable(callableVal, numArgs);
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
          ObjClass *klass = newClass(AS_STRING(className), NULL);
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
              runtimeError("Tried to access property '%s' on non-instance (type: %s)", propStr->chars, typeOf(instance));
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
          } else { // TODO: pop frames to try
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
          if (!isThrowable(tblRow->lastThrownValue)) {
              fprintf(stderr, "Non-throwable found: %s\n", typeOf(tblRow->lastThrownValue));
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
    frame->ip = chunk->code;
    frame->slots = vm.stack;
    frame->function = newFunction(chunk);

    InterpretResult result = run();
    return result;
}
