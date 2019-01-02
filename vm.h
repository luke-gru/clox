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

typedef struct CallFrame {
    // Non-native function fields
    ObjClosure *closure; // if call frame is from compiled code, this is set
    uint8_t *ip; // ip into closure's bytecode chunk, if callable is not a C function
    int start; // starting instruction offset in parent (for throw/catch)
    Value *slots; // local variables and function arguments
    ObjInstance *instance; // if function is a method
    ObjClass *klass; // if function is a method

    // Native (C) function fields
    bool isCCall; // native call, callframe created for C (native) function call
    ObjNative *nativeFunc; // only if isCCall is true

    int callLine;
    ObjString *file; // full path of file the function is called from
    jmp_buf jmpBuf; // only used if chunk associated with closure has a catch table
    bool jmpBufSet;
    struct CallFrame *prev;
    ObjFunction *block; // if block, this is the block function
    ObjFunction *lastBlock;
    int stackAdjustOnPop; // used for blocks
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
// This is per-thread
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
    bool evalContext; // is executing eval()
    bool loadContext; // is executing 'loadScript' or 'requireScript'
} VMExecContext;

// thread internals
typedef enum ThreadStatus {
    THREAD_STOPPED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_ZOMBIE,
} ThreadStatus;

#define THREAD_OPS_UNTIL_SWITCH 10000

typedef struct LxThread {
    pthread_t tid;
    ThreadStatus status;
    VMExecContext *ec; // current execution context of vm
    vec_void_t v_ecs; // stack of execution contexts. Top of stack is current context.
    ObjUpvalue *openUpvalues; // linked list of upvalue objects to keep alive
    Obj *thisObj;
    ObjFunction *curBlock;
    ObjFunction *lastBlock;
    ObjFunction *outermostBlock;
    Value *lastValue;
    bool hadError;
    ErrTagInfo *errInfo;
    Value lastErrorThrown; // TODO: change to Obj pointer
    int inCCall;
    bool cCallThrew;
    bool returnedFromNativeErr;
    jmp_buf cCallJumpBuf;
    bool cCallJumpBufSet;
    int vmRunLvl;
    int lastSplatNumArgs;
    // stack of object pointers created during C function calls. When
    // control returns to the VM, these are popped. Stack objects aren't
    // collected during GC.
    vec_void_t stackObjects;
    int mutexCounter;
    pthread_mutex_t sleepMutex;
    pthread_cond_t sleepCond;
    int opsRemaining;
} LxThread;

// threads
void threadSetStatus(Value thread, ThreadStatus status);
void threadSetId(Value thread, pthread_t tid);
ThreadStatus threadGetStatus(Value thread);
pthread_t threadGetId(Value thread);
bool isOnlyThread(void);

typedef struct VM {
    Table globals; // global variables
    Table strings; // interned strings
    ObjString *initString;
    ObjString *fileString;
    ObjString *dirString;
    ObjString *printBuf;
    bool printToStdout;

    // GC fields
    int grayCount;
    int grayCapacity;
    Obj **grayStack;
    vec_void_t hiddenObjs;

    vec_val_t loadedScripts;

    Debugger debugger;
    bool instructionStepperOn;

    bool inited;

    volatile bool exited;

    vec_void_t exitHandlers;

    // threading
    pthread_mutex_t GVLock; // global VM lock
    pthread_cond_t GVCond;
    int GVLockStatus;
    volatile LxThread *curThread;
    LxThread *mainThread;
    vec_void_t threads; // list of current thread ObjInstance pointers
    int lastOp; // for debugging when error
} VM; // singleton

extern VM vm;
extern volatile bool settingUpThread;

#define EC (THREAD()->ec)
LxThread *THREAD();
LxThread *FIND_THREAD(pthread_t tid);
LxThread *FIND_NEW_THREAD();
ObjInstance *FIND_THREAD_INSTANCE(pthread_t tid);

#define GVL_UNLOCK_BEGIN() do { \
  LxThread *thStored = THREAD(); \
  pthread_mutex_unlock(&vm.GVLock); \
  vm.curThread = NULL; GVLOwner = -1; \
  vm.GVLockStatus = 0

#define GVL_UNLOCK_END() \
  pthread_mutex_lock(&vm.GVLock); \
  threadSetCurrent(thStored); \
} while(0)

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
Value VMEvalNoThrow(const char *src, const char *filename, int lineno);
Value *getLastValue(void);

// script loading
bool VMLoadedScript(char *fname);

// operand stack
void push(Value value); // push onto operand stack
Value pop(void); // pop top of operand stack
Value peek(unsigned);
void resetStack(void); // reset operand stack

NORETURN void repl(void);

// errors
void setBacktrace(Value err);
NORETURN void throwErrorFmt(ObjClass *klass, const char *format, ...);
#define throwArgErrorFmt(format, ...) throwErrorFmt(lxArgErrClass, format, __VA_ARGS__)
NORETURN void throwError(Value err);
ErrTagInfo *addErrInfo(ObjClass *errClass);
typedef void* (vm_cb_func)(void*);
void *vm_protect(vm_cb_func func, void *arg, ObjClass *errClass, ErrTag *status);
void unwindJumpRecover(ErrTagInfo *info);
void rethrowErrInfo(ErrTagInfo *info);
void unsetErrInfo(void);
void popErrInfo(void);
void errorPrintScriptBacktrace(const char *format, ...);

// calling functions/methods
// low-level call function, arguments must be pushed to stack, including
// callable if it's not a method, or the instance if it is. Argcount does not
// include the instance, if it's a method. `cinfo` can be NULL.
bool callCallable(Value callable, int argCount, bool isMethod, CallInfo *cinfo);
// higher-level call function, but callable must be provided. Return value is
// pushed to stack. argCount does not include instance.
Value callVMMethod(
    ObjInstance *instance, Value callable,
    int argCount, Value *args
);
// high-level call function: instance and method name given, if no method then
// error is raised. Value is returned, popped from stack. argCount does not
// include instance.
Value callMethod(Obj *instance, ObjString *methodName, int argCount, Value *args);
// Nothing should be pushed to stack. On return, value is popped from stack.
// Callable should not be a method.
Value callFunctionValue(Value callable, int argCount, Value *args);
// Must be called from native C callframe only. Value is returned, popped from
// stack. `args` does not include `self`, and `cinfo` can be NULL.
Value callSuper(int argCount, Value *args, CallInfo *cinfo);
// NOTE: must be called before lxYield to setup error handlers.
// Similar code to vm_protect.
#define SETUP_BLOCK(status) {\
    ASSERT(THREAD()->curBlock);\
    addErrInfo(lxErrClass);\
    ErrTagInfo *errInfo = THREAD()->errInfo;\
    ObjFunction *blk = THREAD()->curBlock;\
    int jmpres = 0;\
    if ((jmpres = setjmp(errInfo->jmpBuf)) == JUMP_SET) {\
        status = TAG_NONE;\
    } else if (jmpres == JUMP_PERFORMED) {\
        unwindJumpRecover(errInfo);\
        THREAD()->curBlock = blk;\
        ASSERT(THREAD()->errInfo == errInfo);\
        errInfo->status = TAG_RAISE;\
        errInfo->caughtError = THREAD()->lastErrorThrown;\
        status = TAG_RAISE;\
        popErrInfo();\
    }\
}


// call frames
void popFrame(void);
CallFrame *pushFrame(void);
CallFrame *getFrame(void);

// upvalues
ObjUpvalue *captureUpvalue(Value *local);

// iterators
Value createIterator(Value iterable);

// threads
void acquireGVL(void);
void acquireGVLTid(pthread_t tid);
void acquireGVLMaybe(void);
void releaseGVL(void);
void thread_debug(int lvl, const char *format, ...);
volatile long long GVLOwner;
void threadSetCurrent(LxThread *th);

// debug
void printVMStack(FILE *f, LxThread *th);
void setPrintBuf(ObjString *buf, bool alsoStdout); // `print` will output given strings to this buffer, if given
void unsetPrintBuf(void);
int VMNumStackFrames(void);
int VMNumCallFrames(void);
const char *callFrameName(CallFrame *frame);
void debugFrame(CallFrame *frame);

// exiting
void runAtExitHooks(void);
NORETURN void stopVM(int status);

#endif
