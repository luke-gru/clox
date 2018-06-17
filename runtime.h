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
bool runtimeCheckArgs(int min, int max, int actual);

#endif
