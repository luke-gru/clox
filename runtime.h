#ifndef _clox_runtime_h
#define _clox_runtime_h

#include "value.h"
#include "vm.h"

#define CHECK_ARGS(func, min, max, actual) do {\
    if (!runtimeCheckArgs(min, max, actual)) {\
        throwArgErrorFmt("Error in %s, expected %d to %d args, got %d",\
            func, min, max, actual);\
        return NIL_VAL;\
    }\
    } while (0)

extern const char pathSeparator;

Value runtimeNativeClock(int argCount, Value *args);
Value runtimeNativeTypeof(int argCount, Value *args);
Value lxLoadScript(int argCount, Value *args);
Value lxRequireScript(int argCount, Value *args);
Value lxDebugger(int argCount, Value *args);

bool runtimeCheckArgs(int min, int max, int actual);

Value lxArrayInit(int argCount, Value *args);
Value lxArrayPush(int argCount, Value *args);
Value lxArrayToString(int argCount, Value *args);
Value lxArrayIndexGet(int argCount, Value *args);
Value lxArrayIndexSet(int argCount, Value *args);

Value lxMapInit(int argCount, Value *args);
Value lxMapIndexGet(int argCount, Value *args);
Value lxMapIndexSet(int argCount, Value *args);
Value lxMapKeys(int argCount, Value *args);
Value lxMapValues(int argCount, Value *args);
Value lxMapToString(int argCount, Value *args);

Value lxErrInit(int argCount, Value *args);
#endif
