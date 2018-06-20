#ifndef clox_vm_h
#define clox_vm_h

#include <stdio.h>
#include "chunk.h"
#include "object.h"
#include "table.h"
#include "debug.h"

#define STACK_MAX 256
#define FRAMES_MAX 64

typedef struct {
  ObjFunction *function;
  uint8_t *ip;
  int start; // starting instruction offset in parent (for throw/catch)
  Value *slots;
} CallFrame; // represents a local scope (block, function, etc)

typedef struct {
  Value stack[STACK_MAX]; // stack VM, this is the stack of operands
  Value *stackTop;
  CallFrame frames[FRAMES_MAX]; // NOTE: callframe contains chunk!
  unsigned frameCount;
  Obj *objects;
  Value *lastValue;
  Table globals; // global variables
  Table strings; // interned strings
  ObjString *initString;
  bool hadError;
  ObjString *printBuf;

  // GC fields
  size_t bytesAllocated;
  size_t nextGCThreshhold;
  int grayCount;
  int grayCapacity;
  Obj **grayStack;

  vec_void_t hiddenObjs;
  bool inited;
} VM; // singleton

extern VM vm;

typedef enum {
  INTERPRET_OK,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM();
void freeVM();
InterpretResult interpret(Chunk *chunk);
Value *getLastValue();
Value callVMMethod(
    ObjInstance *instance, Value callable,
    int argCount, Value *args
);
void push(Value value);
Value pop();
void runtimeError(const char *format, ...);

void setPrintBuf(ObjString *buf);
void unsetPrintBuf(void);

NORETURN void repl(void);
void resetStack();
int VMNumStackFrames(void);
void printVMStack(FILE *f);

#endif
