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
    initTable(&vm.globals);
    initTable(&vm.strings);
}

void freeVM() {
}

static bool isStackEmpty() {
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

Value *getLastValue() {
    if (isStackEmpty()) {
        return NULL;
    } else {
        return vm.stackTop-1;
    }
}

Value nilValue() {
    Value v = {
        .type = VAL_NIL,
    };
    v.as.number = 0;
    return v;
}

/**
 * Run the VM's instructions.
 */
static InterpretResult run(void) {
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
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
        printDisassembledInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
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
      case OP_RETURN:
        /*fprintf(stderr, "returning\n");*/
        return INTERPRET_OK;
      default:
        printf("Unknown opcode instruction: %s\n", opName(instruction));
        return INTERPRET_RUNTIME_ERROR;
    }
  }

  return INTERPRET_RUNTIME_ERROR;
#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult interpret(Chunk *chunk) {
    vm.chunk = chunk;
    vm.ip = vm.chunk->code;

    InterpretResult result = run();
    return result;
}
