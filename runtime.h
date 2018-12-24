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
extern bool isClassHierarchyCreated;

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
Value lxSleep(int argCount, Value *args);
Value lxExit(int argCount, Value *args);
Value lxNewThread(int argCount, Value *args);
Value lxJoinThread(int argCount, Value *args);
Value lxAtExit(int argCount, Value *args);

// class Object

Value lxObjectInit(int argCount, Value *args);
Value lxObjectGetClass(int argCount, Value *args);
Value lxObjectGetSingletonClass(int argCount, Value *args);
Value lxObjectGetObjectId(int argCount, Value *args);
Value lxObjectDup(int argCount, Value *args);
Value lxObjectExtend(int argCount, Value *args);
ObjNative *nativeObjectInit;

// class Module
Value lxModuleInit(int argCount, Value *args);
ObjNative *nativeModuleInit;

// class Class
Value lxClassInit(int argCount, Value *args);
Value lxClassInclude(int argCount, Value *args);
Value lxClassGetSuperclass(int argCount, Value *args);
Value lxClassGetName(int argCount, Value *args);
//Value lxClassAncestors(int argCount, Value *args);
ObjNative *nativeClassInit;

// class String
ObjNative *nativeStringInit;

// class Array
ObjNative *nativeArrayInit;

// class Map
ObjNative *nativeMapInit;

// class Iterator
Value lxIteratorInit(int argCount, Value *args);
Value lxIteratorNext(int argCount, Value *args);
//Value lxIteratorRewind(int argCount, Value *args);
//Value lxIteratorIsAtEnd(int argCount, Value *args);
ObjNative *nativeIteratorInit;

// class Thread
Value lxThreadInit(int argCount, Value *args);
ObjNative *nativeThreadInit;

// module GC
Value lxGCStats(int argCount, Value *args);
Value lxGCCollect(int argCount, Value *args);

// class Error
Value lxErrInit(int argCount, Value *args);
ObjNative *nativeErrorInit;

// class String
void Init_StringClass(void);
// class Array
void Init_ArrayClass(void);
// class Map
void Init_MapClass(void);
// module IO
void Init_IOModule(void);
// class File
void Init_FileClass(void);
// module Process
void Init_ProcessModule(void);

// API for adding classes/modules/methods
void addGlobalFunction(const char *name, NativeFn func);
ObjClass *addGlobalClass(const char *name, ObjClass *super);
ObjModule *addGlobalModule(const char *name);
ObjNative *addNativeMethod(void *klass, const char *name, NativeFn func);
ObjNative *addNativeGetter(void *klass, const char *name, NativeFn func);
ObjNative *addNativeSetter(void *klass, const char *name, NativeFn func);
#endif
