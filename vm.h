#ifndef clox_vm_h
#define clox_vm_h

#include <stdio.h>
#include "chunk.h"
#include "object.h"
#include "table.h"
#include "debug.h"

#define STACK_MAX 256
#define FRAMES_MAX 64

typedef struct CallFrame {
    // Non-native function fields
    ObjClosure *closure; // if call frame is from compiled code, this is set
    uint8_t *ip;
    int start; // starting instruction offset in parent (for throw/catch)
    Value *slots;

    // Native (C) function fields
    bool isCCall; // native call, callframe created for C (native) function call
    ObjNative *nativeFunc; // only if isCCall is true
} CallFrame; // represents a local scope (block, function, etc)

// Execution context for VM. When loading a script, a new context is created.
// When evaling, a new context is also created.
typedef struct VMExecContext {
    Value stack[STACK_MAX]; // stack VM, this is the stack of operands
    Value *stackTop;
    // NOTE: the current callframe contains the closure, which contains the
    // function, which contains the chunk of bytecode for the current function
    // (or top-level)
    CallFrame frames[FRAMES_MAX];
    unsigned frameCount;
    Table roGlobals; // per-script readonly global vars
} VMExecContext;

typedef vec_t(VMExecContext) vec_VM_EC_t;

typedef struct VM {
    VMExecContext *ec; // current execution context
    vec_VM_EC_t v_ecs; // stack of execution contexts. Top of stack is current context.

    Obj *objects; // linked list of heap objects
    ObjUpvalue *openUpvalues; // linked list of upvalue objects to keep alive
    Value *lastValue;
    Table globals; // global variables
    Table strings; // interned strings
    ObjString *initString;
    ObjString *fileString;
    ObjString *dirString;
    ObjString *printBuf;

    // GC fields
    size_t bytesAllocated;
    size_t nextGCThreshhold;
    int grayCount;
    int grayCapacity;
    Obj **grayStack;
    vec_void_t hiddenObjs;
    // stack of object pointers created during C function calls. When
    // control returns to the VM, these are popped. Stack objects aren't
    // collected during GC.
    vec_void_t stackObjects;

    Value lastErrorThrown;
    vec_val_t loadedScripts;

    bool inited;
    bool hadError;
} VM; // singleton

extern VM vm;

typedef enum {
  INTERPRET_OK = 1,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM();
void freeVM();
InterpretResult interpret(Chunk *chunk, char *filename);
InterpretResult loadScript(Chunk *chunk, char *filename);
bool VMLoadedScript(char *fname);
Value *getLastValue();
Value callVMMethod(
    ObjInstance *instance, Value callable,
    int argCount, Value *args
);
void push(Value value); // push onto operand stack
Value pop(); // pop top of operand stack
void runtimeError(const char *format, ...);

void setPrintBuf(ObjString *buf); // `print` will output given strings to this buffer, if given
void unsetPrintBuf(void);

NORETURN void repl(void);
void resetStack(); // reset operand stack
int VMNumStackFrames(void);
void printVMStack(FILE *f);
void setBacktrace(Value err);

void throwError(Value err);
void throwArgErrorFmt(const char *format, ...);
bool callCallable(Value callable, int argCount, bool isMethod);

#endif
