#ifndef _clox_runtime_h
#define _clox_runtime_h

#include "value.h"
#include "vm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHECK_ARITY(func, min, max, actual) do {\
    if (UNLIKELY(!checkArity(min, max, actual))) {\
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
Value lxClassof(int argCount, Value *args);
Value lxLoadScript(int argCount, Value *args);
Value lxRequireScript(int argCount, Value *args);
Value lxDebugger(int argCount, Value *args);
Value lxEval(int argCount, Value *args);
Value lxSleep(int argCount, Value *args);
void threadSleepNano(LxThread *th, int secs);
Value lxYield(int argCount, Value *args);
Value lxBlockGiven(int argCount, Value *args);
Value yieldFromC(int argCount, Value *args, ObjInstance *blkObj);
Value lxExit(int argCount, Value *args);
Value lx_Exit(int argCount, Value *args);
Value lxNewThread(int argCount, Value *args);
Value lxJoinThread(int argCount, Value *args);
Value lxAtExit(int argCount, Value *args);
Value lxAutoload(int argCount, Value *args);

// class Object

Value lxObjectInit(int argCount, Value *args);
Value lxObjectFreeze(int argCount, Value *args);
Value lxObjectUnfreeze(int argCount, Value *args);
Value lxObjectIsFrozen(int argCount, Value *args);
Value lxObjectDup(int argCount, Value *args);
Value lxObjectExtend(int argCount, Value *args);
Value lxObjectHashKey(int argCount, Value *args);
Value lxObjectOpEquals(int argCount, Value *args);
Value lxObjectIsSame(int argCount, Value *args);
Value lxObjectSend(int argCount, Value *args);
Value lxObjectGetProperty(int argCount, Value *args);
Value lxObjectSetProperty(int argCount, Value *args);
Value lxObjectGetClass(int argCount, Value *args);
Value lxObjectGetSingletonClass(int argCount, Value *args);
Value lxObjectGetObjectId(int argCount, Value *args);
extern ObjNative *nativeObjectInit;

// class Module
Value lxModuleInit(int argCount, Value *args);
extern ObjNative *nativeModuleInit;

// class Class
Value lxClassInit(int argCount, Value *args);
Value lxClassInclude(int argCount, Value *args);
Value lxClassGetSuperclass(int argCount, Value *args);
Value lxClassGetName(int argCount, Value *args);
Value lxClassMethodAdded(int argCount, Value *args);
Value lxClassConstDefined(int argCount, Value *args);
Value lxClassConstants(int argCount, Value *args);
Value lxClassConstGet(int argCount, Value *args);
Value lxClassAncestors(int argCount, Value *args);
Value lxClassIsA(int argCount, Value *args);
extern ObjNative *nativeClassInit;

// class String
extern ObjNative *nativeStringInit;

// class Array
extern ObjNative *nativeArrayInit;

// class Map
extern ObjNative *nativeMapInit;

// class Iterator
Value lxIteratorInit(int argCount, Value *args);
Value lxIteratorNext(int argCount, Value *args);
//Value lxIteratorRewind(int argCount, Value *args);
//Value lxIteratorIsAtEnd(int argCount, Value *args);
extern ObjNative *nativeIteratorInit;

// class Thread
Value lxThreadInit(int argCount, Value *args);
extern ObjNative *nativeThreadInit;

// class Block
extern ObjNative *nativeBlockInit;

// class Regex
extern ObjNative *nativeRegexInit;

// module GC
Value lxGCStats(int argCount, Value *args);
Value lxGCCollect(int argCount, Value *args);
Value lxGCCollectYoung(int argCount, Value *args);
Value lxGCSetFinalizer(int argCount, Value *args);
Value lxGCOff(int argCount, Value *args);
Value lxGCOn(int argCount, Value *args);

// class Error
Value lxErrInit(int argCount, Value *args);
extern ObjNative *nativeErrorInit;
ObjClass *sysErrClass(int err);

// class String
void Init_StringClass(void);
// class Array
void Init_ArrayClass(void);
// class Map
void Init_MapClass(void);
void Init_RegexClass(void);
// class IO
void Init_IOClass(void);
LxFile *fileGetHidden(Value io);
LxFile *fileGetInternal(Value io);
LxFile *initIOAfterOpen(Value io, ObjString *fname, int fd, int mode, int oflags);
size_t IOWrite(Value io, const void *buf, size_t count);
void IOClose(Value io);
ObjString *IORead(Value io, size_t bytesMax, bool untilEOF);
ObjString *IOReadFd(int fd, size_t bytesMax, bool untilEOF);
//ObjString *IOGetline(Value io, size_t bytesMax);
//ObjString *IOGetchar(Value io);

void Init_FileClass(void);
void Init_DirClass(void);
void Init_ProcessModule(void);
void Init_SignalModule(void);
// random()/srandom() functions
void Init_rand(void);
void Init_ThreadClass(void);
void Init_BlockClass(void);
void Init_TimeClass(void);

// API for adding classes/modules/methods
void addGlobalFunction(const char *name, NativeFn func);
ObjClass *addGlobalClass(const char *name, ObjClass *super);
ObjModule *addGlobalModule(const char *name);
ObjNative *addNativeMethod(void *klass, const char *name, NativeFn func);
ObjNative *addNativeGetter(void *klass, const char *name, NativeFn func);
ObjNative *addNativeSetter(void *klass, const char *name, NativeFn func);

// API for adding constants
void addConstantUnder(const char *name, Value constVal, Value owner);
bool findConstantUnder(ObjClass *klass, ObjString *name, Value *valOut);

// getting/setting properties
Value propertyGet(ObjInstance *obj, ObjString *propName);
void propertySet(ObjInstance *obj, ObjString *propName, Value rval);

#ifdef __cplusplus
}
#endif

#endif
