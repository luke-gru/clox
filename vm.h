#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "object.h"

#define STACK_MAX 256

typedef struct {
  Chunk *chunk; // bytecode instructions
  uint8_t *ip; // pointer to current instruction
  Value stack[STACK_MAX]; // stack VM, this is the stack of operands
  Value *stackTop;
  struct sObj *objects;
  Value *lastValue;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM();
void freeVM();
InterpretResult interpret(Chunk *chunk);
Value *getLastValue();
void push(Value value);
Value pop();

#endif
