#ifndef _clox_runtime_h
#define _clox_runtime_h

#include "value.h"
#include "vm.h"

#define CHECK_ARGS(func, min, max, actual) do {\
    if (!runtimeCheckArgs(min, max, actual)) {\
        runtimeError("Error in %s, expected %d to %d args, got %d",\
            func, min, max, actual);\
        return NIL_VAL;\
    }\
    } while (0)


Value runtimeNativeClock(int argCount, Value *args);
Value runtimeNativeTypeof(int argCount, Value *args);
bool runtimeCheckArgs(int min, int max, int actual);

Value lxArrayInit(int argCount, Value *args);
Value lxArrayPush(int argCount, Value *args);
Value lxArrayToString(int argCount, Value *args);
Value lxArrayIndexGet(int argCount, Value *args);
Value lxArrayIndexSet(int argCount, Value *args);
Value lxMapInit(int argCount, Value *args);
#endif
