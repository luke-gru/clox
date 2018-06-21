#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"
#include "table.h"

typedef enum ObjType {
  OBJ_T_NONE = 0,
  OBJ_T_STRING,
  OBJ_T_FUNCTION,
  OBJ_T_CLASS,
  OBJ_T_INSTANCE,
  OBJ_T_NATIVE_FUNCTION,
  OBJ_T_BOUND_METHOD,
  OBJ_T_INTERNAL,
} ObjType;

// basic object structure that all objects (values of VAL_T_OBJ type)
typedef struct Obj {
  ObjType type;
  // GC fields
  Obj *next;
  Obj *prev;
  bool isLinked; // is this object linked into vm.objects?
  bool isDark; // is this object marked?
  bool noGC; // don't collect this object

  // Other fields
  bool isFrozen;
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
extern ObjClass *lxMapClass;
extern ObjClass *lxErrClass;

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

#define IS_STRING(value)        isObjType(value, OBJ_T_STRING)
#define IS_FUNCTION(value)      isObjType(value, OBJ_T_FUNCTION)
#define IS_NATIVE_FUNCTION(value) isObjType(value, OBJ_T_NATIVE_FUNCTION)
#define IS_CLASS(value)         isObjType(value, OBJ_T_CLASS)
#define IS_INSTANCE(value)      isObjType(value, OBJ_T_INSTANCE)
#define IS_BOUND_METHOD(value)  isObjType(value, OBJ_T_BOUND_METHOD)
#define IS_INTERNAL(value)      isObjType(value, OBJ_T_INTERNAL)

#define IS_ARRAY(value)         (IS_INSTANCE(value) && AS_INSTANCE(value)->klass == lxAryClass)
#define IS_MAP(value)           (IS_INSTANCE(value) && AS_INSTANCE(value)->klass == lxMapClass)

#define AS_STRING(value)        ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)       (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value)      ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE_FUNCTION(value) ((ObjNative*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)  ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)         ((ObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value)      ((ObjInstance*)AS_OBJ(value))
#define AS_INTERNAL(value)      ((ObjInternal*)AS_OBJ(value))

#define ARRAY_GET(value, idx)    (arrayGet(value, idx))
#define ARRAY_SIZE(value)        (arraySize(value))
#define ARRAY_GETHIDDEN(value)   (arrayGetHidden(value))

#define MAP_GET(value, valkey)   (mapGet(value, valKey))
#define MAP_SIZE(value)          (mapSize(value))
#define MAP_GETHIDDEN(value)     (mapGetHidden(value))

typedef ObjString *(*newStringFunc)(char *chars, int length);
ObjString *takeString(char *chars, int length); // uses provided memory as internal buffer, must be heap memory or will error when GC'ing the object
ObjString *copyString(char *chars, int length); // copies provided memory. Object lives on lox heap.
ObjString *hiddenString(char *chars, int length); // hidden from GC, used in tests mainly.
ObjString *newString(char *chars, int length); // always creates new string in vm.objects
ObjString *newStackString(char *chars, int length); // Used in native C functions. Object first lives in VM arena, conceptually.
ObjString *internedString(char *chars, int length); // Provided string must be interned by VM or will give error.

void objFreeze(Obj*);

// NOTE: don't call pushCString on a string value that's a key to a map! The
// hash value changes and the map won't be able to index it anymore.
void pushCString(ObjString *string, char *chars, int lenToAdd);
uint32_t hashString(char *key, int length);

Value       arrayGet(Value aryVal, int idx);
int         arraySize(Value aryVal);
void        arrayPush(Value aryVal, Value el);
ValueArray *arrayGetHidden(Value aryVal);
Value       newArray(void);

Value       mapGet(Value mapVal, Value key);
Value       mapSize(Value mapVal);
Table      *mapGetHidden(Value mapVal);

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
// directly, instead use the [IS_XXX] macro for the type in question.
static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

const char *typeOfObj(Obj *obj);

#endif
