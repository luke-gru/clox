#ifndef _clox_runtime_h
#define _clox_runtime_h

#include "value.h"
#include "vm.h"

#define CHECK_ARGS(func, min, max, actual) do {\
    if (!runtimeCheckArgs(min, max, actual)) {\
        if (min == max) {\
            throwArgErrorFmt("Error in %s, expected %d arg%s, got %d",\
                func, min, max == 1 ? "" : "s", actual);\
        } else {\
            throwArgErrorFmt("Error in %s, expected %d to %d args, got %d",\
                func, min, max, actual); \
        }\
        return NIL_VAL;\
    }\
    } while (0)

extern const char pathSeparator;

// builtin (native) functions
Value lxClock(int argCount, Value *args);
Value lxTypeof(int argCount, Value *args);
Value lxLoadScript(int argCount, Value *args);
Value lxRequireScript(int argCount, Value *args);
Value lxDebugger(int argCount, Value *args);
Value lxEval(int argCount, Value *args);
Value lxFork(int argCount, Value *args);
Value lxExec(int argCount, Value *args);
Value lxWaitpid(int argCount, Value *args);
Value lxSleep(int argCount, Value *args);
Value lxExit(int argCount, Value *args);
Value lxNewThread(int argCount, Value *args);
Value lxJoinThread(int argCount, Value *args);
Value lxSystem(int argCount, Value *args);
Value lxAtExit(int argCount, Value *args);

bool runtimeCheckArgs(int min, int max, int actual);

// class Object
Value lxObjectGetClass(int argCount, Value *args);
Value lxObjectGetObjectId(int argCount, Value *args);
Value lxObjectDup(int argCount, Value *args);

// class Module
Value lxModuleInit(int argCount, Value *args);

// class Class
Value lxClassInit(int argCount, Value *args);
Value lxClassInclude(int argCount, Value *args);
Value lxClassGetSuperclass(int argCount, Value *args);
Value lxClassGetName(int argCount, Value *args);
//Value lxClassAncestors(int argCount, Value *args);

// class String
Value lxStringInit(int argCount, Value *args);
Value lxStringToString(int argCount, Value *args);
Value lxStringOpAdd(int argCount, Value *args);
Value lxStringPush(int argCount, Value *args);
Value lxStringClear(int argCount, Value *args);
Value lxStringDup(int argCount, Value *args);
Value lxStringInsertAt(int argCount, Value *args);
Value lxStringSubstr(int argCount, Value *args);
Value lxStringIndexGet(int argCount, Value *args);
Value lxStringIndexSet(int argCount, Value *args);

// class Array
Value lxArrayInit(int argCount, Value *args);
Value lxArrayToString(int argCount, Value *args);
Value lxArrayPush(int argCount, Value *args);
Value lxArrayPop(int argCount, Value *args);
Value lxArrayPushFront(int argCount, Value *args);
Value lxArrayPopFront(int argCount, Value *args);
Value lxArrayIndexGet(int argCount, Value *args);
Value lxArrayIndexSet(int argCount, Value *args);
Value lxArrayIter(int argCount, Value *args);
Value lxArrayDelete(int argCount, Value *args);
Value lxArrayClear(int argCount, Value *args);
Value lxArrayOpEquals(int argCount, Value *args);

// class Map
Value lxMapInit(int argCount, Value *args);
Value lxMapToString(int argCount, Value *args);
Value lxMapIndexGet(int argCount, Value *args);
Value lxMapIndexSet(int argCount, Value *args);
Value lxMapKeys(int argCount, Value *args);
Value lxMapValues(int argCount, Value *args);
Value lxMapToString(int argCount, Value *args);
Value lxMapIter(int argCount, Value *args);
Value lxMapClear(int argCount, Value *args);
Value lxMapOpEquals(int argCount, Value *args);

// class Iterator
Value lxIteratorInit(int argCount, Value *args);
Value lxIteratorNext(int argCount, Value *args);
//Value lxIteratorRewind(int argCount, Value *args);
//Value lxIteratorIsAtEnd(int argCount, Value *args);

// class File
Value lxFileReadStatic(int argCount, Value *args);

// class Thread
Value lxThreadInit(int argCount, Value *args);

// module GC
Value lxGCStats(int argCount, Value *args);
Value lxGCCollect(int argCount, Value *args);

// class Error
Value lxErrInit(int argCount, Value *args);
#endif
