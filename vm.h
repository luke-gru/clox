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
  ObjClosure *closure; // if call frame is from compiled code, this is set
  uint8_t *ip;
  int start; // starting instruction offset in parent (for throw/catch)
  Value *slots;

  bool isCCall; // native call, callframe created for C (native) function call
  ObjNative *nativeFunc; // only if isCCall is true
} CallFrame; // represents a local scope (block, function, etc)

typedef struct {
  Value stack[STACK_MAX]; // stack VM, this is the stack of operands
  Value *stackTop;
  CallFrame frames[FRAMES_MAX]; // NOTE: callframe contains chunk!
  unsigned frameCount;
  Obj *objects; // linked list of heap objects
  ObjUpvalue *openUpvalues; // linked list of upvalue objects to keep alive
  Value *lastValue;
  Table globals; // global variables
  Table strings; // interned strings
  ObjString *initString;
  ObjString *printBuf;

  // GC fields
  size_t bytesAllocated;
  size_t nextGCThreshhold;
  int grayCount;
  int grayCapacity;
  Obj **grayStack;
  vec_void_t hiddenObjs;
  vec_void_t stackObjects; // stack of object pointers created during C function calls.

  bool inited;
  bool hadError;

  Value lastErrorThrown;
} VM; // singleton

extern VM vm;

typedef enum {
  INTERPRET_OK = 1,
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

void throwError(Value err);
void throwArgErrorFmt(const char *format, ...);
bool callCallable(Value callable, int argCount, bool isMethod);

#endif
