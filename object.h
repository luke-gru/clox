#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"
#include "table.h"

typedef enum ObjType {
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_CLASS,
  OBJ_INSTANCE,
  OBJ_NATIVE_FUNCTION,
  OBJ_BOUND_METHOD,
  OBJ_INTERNAL,
} ObjType;

typedef struct Obj {
  ObjType type;
  // GC fields
  Obj *next;
  bool isDark; // is this object marked?
  bool noGC; // don't collect this object
} Obj;

typedef struct ObjString {
  Obj object;
  int length;
  char *chars;
  uint32_t hash;
} ObjString;


typedef void (*GCMarkFunc)(Obj *obj);
typedef void (*GCFreeFunc)(Obj *obj);

typedef struct ObjInternal {
  Obj object;
  void *data; // internal data
  GCMarkFunc markFunc;
  GCMarkFunc freeFunc;
} ObjInternal;

typedef struct ObjFunction {
  Obj object;
  int arity;
  //int upvalueCount;
  // NOTE: needs to be a value (non-pointer), as it's saved directly in the parent chunk as a constant value
  // and needs to be read by the VM, or serialized/loaded to/from disk.
  Chunk chunk;
  ObjString *name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct ObjNative {
  Obj object;
  NativeFn function;
  ObjString *name;
} ObjNative;

typedef struct ObjClass ObjClass;
typedef struct ObjClass {
  Obj object;
  ObjString *name;
  ObjClass *superclass;
  Table methods;
} ObjClass;

extern ObjClass *lxObjClass;
extern ObjClass *lxAryClass;

typedef struct ObjInstance {
  Obj object;
  ObjClass *klass;
  Table fields;
  Table hiddenFields;
} ObjInstance;

typedef struct ObjBoundMethod {
  Obj object;
  Value receiver;
  Obj *callable;
} ObjBoundMethod;

#define IS_STRING(value)        isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value)      isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE_FUNCTION(value) isObjType(value, OBJ_NATIVE_FUNCTION)
#define IS_CLASS(value)         isObjType(value, OBJ_CLASS)
#define IS_INSTANCE(value)      isObjType(value, OBJ_INSTANCE)
#define IS_BOUND_METHOD(value)  isObjType(value, OBJ_BOUND_METHOD)
#define IS_INTERNAL(value)  isObjType(value, OBJ_INTERNAL)

#define IS_ARRAY(value)         (IS_INSTANCE(value) && AS_INSTANCE(value)->klass == lxAryClass)

#define AS_STRING(value)        ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)       (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value)      ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE_FUNCTION(value) ((ObjNative*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)  ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)         ((ObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value)      ((ObjInstance*)AS_OBJ(value))
#define AS_INTERNAL(value)      ((ObjInternal*)AS_OBJ(value))

ObjString *takeString(char *chars, int length);
ObjString *copyString(const char *chars, int length);
ObjString *newString(char *chars, int length);
ObjString *internedString(const char *chars);
void pushCString(ObjString *string, char *chars, int lenToAdd);

ObjFunction *newFunction();
void freeFunction(ObjFunction *func);
ObjClass *newClass(ObjString *name, ObjClass *superclass);
ObjInstance *newInstance(ObjClass *klass);
ObjNative *newNative(ObjString *name, NativeFn function);
ObjBoundMethod *newBoundMethod(ObjInstance *receiver, Obj *callable);
ObjInternal *newInternalObject(void *data, GCMarkFunc markFn, GCFreeFunc freeFn);
void *internalGetData(ObjInternal *obj);

Obj *instanceFindMethod(ObjInstance *obj, ObjString *name);

// Returns true if [value] is an object of type [type]. Do not call this
// directly, instead use the [IS___] macro for the type in question.
static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

const char *typeOfObj(Obj *obj);

#endif
