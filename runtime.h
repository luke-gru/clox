#ifndef _clox_runtime_h
#define _clox_runtime_h

#include "value.h"
#include "vm.h"

#define CHECK_ARITY(func, min, max, actual) do {\
    if (!checkArity(min, max, actual)) {\
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

// ex: CHECK_ARG_BUILTIN_TYPE(value, IS_BOOL_FUNC, "bool", 1);
#define CHECK_ARG_BUILTIN_TYPE(value, typechk_p, typenam, argnum) checkBuiltinArgType(value, typechk_p, typenam, argnum)
#define CHECK_ARG_IS_INSTANCE_OF(value, klass, argnum) checkArgIsInstanceOf(value, klass, argnum)
#define CHECK_ARG_IS_A(value, klass, argnum) checkArgIsA(value, klass, argnum)

extern const char pathSeparator;

// Error-checking functions with macros
bool checkArity(int min, int max, int actual);
void checkBuiltinArgType(Value arg, value_type_p typechk_p, const char *typeExpect, int argnum);
void checkArgIsInstanceOf(Value arg, ObjClass *klass, int argnum);
void checkArgIsA(Value arg, ObjClass *klass, int argnum);

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
Value lxStringPush(int argCount, Value *args);
Value lxStringClear(int argCount, Value *args);
Value lxStringDup(int argCount, Value *args);
Value lxStringInsertAt(int argCount, Value *args);
Value lxStringSubstr(int argCount, Value *args);
Value lxStringOpAdd(int argCount, Value *args);
Value lxStringOpIndexGet(int argCount, Value *args);
Value lxStringOpIndexSet(int argCount, Value *args);
Value lxStringOpEquals(int argCount, Value *args);

// class Array
Value lxArrayInit(int argCount, Value *args);
Value lxArrayToString(int argCount, Value *args);
Value lxArrayPush(int argCount, Value *args);
Value lxArrayPop(int argCount, Value *args);
Value lxArrayPushFront(int argCount, Value *args);
Value lxArrayPopFront(int argCount, Value *args);
Value lxArrayIter(int argCount, Value *args);
Value lxArrayDelete(int argCount, Value *args);
Value lxArrayClear(int argCount, Value *args);
Value lxArrayOpIndexGet(int argCount, Value *args);
Value lxArrayOpIndexSet(int argCount, Value *args);
Value lxArrayOpEquals(int argCount, Value *args);

// class Map
Value lxMapInit(int argCount, Value *args);
Value lxMapToString(int argCount, Value *args);
Value lxMapKeys(int argCount, Value *args);
Value lxMapValues(int argCount, Value *args);
Value lxMapToString(int argCount, Value *args);
Value lxMapIter(int argCount, Value *args);
Value lxMapClear(int argCount, Value *args);
Value lxMapOpEquals(int argCount, Value *args);
Value lxMapOpIndexGet(int argCount, Value *args);
Value lxMapOpIndexSet(int argCount, Value *args);

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

// module Process
void Init_ProcessModule(void);

// API for adding classes/modules/methods
void addGlobalFunction(const char *name, NativeFn func);
ObjClass *addGlobalClass(const char *name, ObjClass *super);
ObjModule *addGlobalModule(const char *name);
void addNativeMethod(void *klass, const char *name, NativeFn func);
void addNativeGetter(void *klass, const char *name, NativeFn func);
void addNativeSetter(void *klass, const char *name, NativeFn func);
#endif
