#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"

#define STACK_MAX 256
#define FRAMES_MAX 64

typedef struct {
/* Calls and Functions not-yet < Closures not-yet
  ObjFunction* function;
*/
//> Closures not-yet
  //ObjClosure* closure;
//< Closures not-yet
  uint8_t *ip;
  Value slots[256];
} CallFrame; // represents a local scope (block, function, etc)

typedef struct {
  Chunk *chunk; // bytecode instructions
  Value stack[STACK_MAX]; // stack VM, this is the stack of operands
  Value *stackTop;
  CallFrame frames[FRAMES_MAX];
  unsigned frameCount;
  struct sObj *objects;
  Value *lastValue;
  Table globals; // global variables
  Table strings; // interned strings
} VM; // singleton

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
