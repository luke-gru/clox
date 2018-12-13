#ifndef clox_vm_h
#define clox_vm_h

#include <pthread.h>
#include <stdio.h>
#include <setjmp.h>
#include "chunk.h"
#include "object.h"
#include "table.h"
#include "debugger.h"
#include "debug.h"

#define STACK_MAX 256
#define FRAMES_MAX 64

#ifndef NDEBUG
#define THREAD_DEBUG(lvl, ...) thread_debug(lvl, __VA_ARGS__)
#else
#define THREAD_DEBUG(lvl, ...) (void)0
#endif

typedef struct CallInfo CallInfo;

extern bool inCCall;


typedef struct CallFrame {
    // Non-native function fields
    ObjClosure *closure; // if call frame is from compiled code, this is set
    uint8_t *ip;
    int start; // starting instruction offset in parent (for throw/catch)
    Value *slots; // local variables and function arguments

    // Native (C) function fields
    bool isCCall; // native call, callframe created for C (native) function call
    ObjNative *nativeFunc; // only if isCCall is true

    int callLine;
    ObjString *file; // full path of file the function is called from
} CallFrame; // represents a local scope (block, function, etc)

typedef enum ErrTag {
    TAG_NONE = 0,
    TAG_RAISE
} ErrTag;

typedef struct ErrTagInfo {
    ErrTag status;
    ObjClass *errClass;
    jmp_buf jmpBuf;
    CallFrame *frame;
    struct ErrTagInfo *prev;
    Value caughtError;
} ErrTagInfo;

// Execution context for VM. When loading a script, a new context is created,
// when evaling a string, a new context is also created, etc.
// Holds the read-only globals like __FILE__ and __DIR__,
// as well as the script name for the currently executing file.
typedef struct VMExecContext {
    Value stack[STACK_MAX]; // stack VM, this is the stack (bottom) of operands
    Value *stackTop;
    // NOTE: the current callframe contains the closure, which contains the
    // function, which contains the chunk of bytecode for the current function
    // (or top-level)
    CallFrame frames[FRAMES_MAX];
    unsigned frameCount;
    Table roGlobals; // per-script readonly global vars (ex: __FILE__)
    ObjString *filename;
    Value *lastValue;
    bool evalContext;
} VMExecContext;

typedef struct VM {
    VMExecContext *ec; // current execution context of vm
    vec_void_t v_ecs; // stack of execution contexts. Top of stack is current context.

    Obj *objects; // linked list of heap objects
    ObjUpvalue *openUpvalues; // linked list of upvalue objects to keep alive
    Value *lastValue;
    Value *thisValue;
    Table globals; // global variables
    Table strings; // interned strings
    ObjString *initString;
    ObjString *fileString;
    ObjString *dirString;
    ObjString *printBuf;
    bool printToStdout;

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

    Debugger debugger;

    bool inited;

    bool exited;
    bool hadError;
    ErrTagInfo *errInfo;

    vec_void_t exitHandlers;

    // threading
    pthread_mutex_t GVLock; // global VM lock
    ObjInstance *curThread;
    ObjInstance *mainThread;
    ObjInstance *threads; // array of current threads
} VM; // singleton

extern VM vm;

#define EC (vm.ec)

typedef enum {
  INTERPRET_OK = 1,
  INTERPRET_UNINITIALIZED, // tried to call interpret() before initVM()
  INTERPRET_RUNTIME_ERROR,
} InterpretResult;

// setup
void initSighandlers(void);

// high-level API
void initVM(void);
void freeVM(void);
InterpretResult interpret(Chunk *chunk, char *filename);
InterpretResult loadScript(Chunk *chunk, char *filename);
Value VMEval(const char *src, const char *filename, int lineno);
Value *getLastValue(void);

bool VMLoadedScript(char *fname);

void push(Value value); // push onto operand stack
Value pop(void); // pop top of operand stack
Value peek(unsigned);

NORETURN void repl(void);
void resetStack(void); // reset operand stack

// debug
void printVMStack(FILE *f);
void setPrintBuf(ObjString *buf, bool alsoStdout); // `print` will output given strings to this buffer, if given
void unsetPrintBuf(void);
int VMNumStackFrames(void);
int VMNumCallFrames(void);

// errors
void setBacktrace(Value err);
void throwErrorFmt(ObjClass *klass, const char *format, ...);
#define throwArgErrorFmt(format, ...) throwErrorFmt(lxArgErrClass, format, __VA_ARGS__)
void throwError(Value err);

ErrTagInfo *addErrInfo(ObjClass *errClass);
typedef void* (vm_cb_func)(void*);
void *vm_protect(vm_cb_func func, void *arg, ObjClass *errClass, ErrTag *status);
void rethrowErrInfo(ErrTagInfo *info);
void unsetErrInfo(void);
void errorPrintScriptBacktrace(const char *format, ...);

// calling functions/methods
bool callCallable(Value callable, int argCount, bool isMethod, CallInfo *info);
Value callVMMethod(
    ObjInstance *instance, Value callable,
    int argCount, Value *args
);

Value createIterator(Value iterable);

void popFrame(void);
CallFrame *pushFrame(void);

NORETURN void stopVM(int status);

// threads
void acquireGVL(void);
void releaseGVL(void);
void thread_debug(int lvl, const char *format, ...);

#endif
