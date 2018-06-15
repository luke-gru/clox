#include <stdio.h>
#include "common.h"
#include "vm.h"
#include "debug.h"
#include "options.h"

VM vm;

void initVM() {
    vm.stackTop = vm.stack;
    vm.objects = NULL;
    vm.lastValue = NULL;
    vm.frameCount = 0;
    initTable(&vm.globals);
    initTable(&vm.strings);
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    // TODO: free object list
    vm.objects = NULL;
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
    return &vm.frames[vm.frameCount-1];
}

static Chunk *currentChunk() {
    ASSERT(getFrame());
    ASSERT(getFrame()->function);
    return &getFrame()->function->chunk;
}

// TODO: throw lox error using setjmp/jmpbuf to allow catches
static void runtimeError(const char* format, ...) {
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

    resetStack();
}

static bool isCallable(Value val) {
    return IS_FUNCTION(val);
}

static const char *typeOfObj(Obj *obj) {
    switch (obj->type) {
    case OBJ_STRING:
        return "string";
    case OBJ_FUNCTION:
        return "function";
    default:
        ASSERT(0);
        return "unknown";
    }
}

static const char *typeOf(Value val) {
    if (IS_BOOL(val)) return "bool";
    if (IS_NIL(val)) return "nil";
    if (IS_NUMBER(val)) return "number";
    if (IS_OBJ(val)) {
        return typeOfObj(AS_OBJ(val));
    } else {
        ASSERT(0);
        return "unknown!";
    }
}

static bool callCallable(ObjFunction *function, int argCount) {
    if (argCount != function->arity) {
        runtimeError("Expected %d arguments but got %d.",
            function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    // add frame
    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->function = function;
    frame->ip = function->chunk.code;

    // +1 to include either the called function or the receiver.
    frame->slots = vm.stackTop - (argCount + 1);
    return true;
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
      push(NUMBER_VAL(AS_NUMBER(a) op AS_NUMBER(b))); \
    } while (0)

  for (;;) {

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

    uint8_t instruction = READ_BYTE();
    switch (instruction) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      case OP_ADD:      BINARY_OP(+); break;
      case OP_SUBTRACT: BINARY_OP(-); break;
      case OP_MULTIPLY: BINARY_OP(*); break;
      case OP_DIVIDE:   BINARY_OP(/); break;
      case OP_NEGATE: {
        Value val = pop();
        push(NUMBER_VAL(-AS_NUMBER(val)));
        break;
      }
      case OP_LESS: {
        Value rhs = pop(); // rhs
        Value lhs = pop(); // lhs
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
            push(nilValue()); // TODO: error out
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
          ObjFunction *func = AS_FUNCTION(callableVal);
          ASSERT(func);
          /*fprintf(stderr, "Calling function\n");*/
          callCallable(func, numArgs);
          break;
      }
      // return from function/method
      case OP_RETURN: {
        Value result = pop();
        vm.stackTop = getFrame()->slots;
        vm.frameCount--;
        push(result);
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
