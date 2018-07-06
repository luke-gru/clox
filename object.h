#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"
#include "table.h"

typedef enum ObjType {
  OBJ_T_NONE = 0,
  OBJ_T_STRING, // TODO: make strings instances
  OBJ_T_FUNCTION,
  OBJ_T_CLASS,
  OBJ_T_INSTANCE,
  OBJ_T_NATIVE_FUNCTION,
  OBJ_T_BOUND_METHOD,
  OBJ_T_UPVALUE,
  OBJ_T_CLOSURE,
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
  int upvalueCount;
  // NOTE: needs to be a value (non-pointer), as it's saved directly in the parent chunk as a constant value
  // and needs to be read by the VM, or serialized/loaded to/from disk.
  Chunk chunk;
  ObjString *name;
  bool isMethod;
  bool isSingletonMethod;
} ObjFunction;

typedef struct ObjUpvalue ObjUpvalue;
typedef struct ObjUpvalue {
  Obj object;

  // Pointer to the variable this upvalue is referencing.
  Value *value;

  // If the upvalue is closed (i.e. the local variable it was pointing to has
  // been popped off the stack) then the closed-over value is hoisted out of
  // the stack into here. field [value] is then be changed to point to this.
  Value closed;

  // Open upvalues are stored in a linked list.
  ObjUpvalue *next;
} ObjUpvalue;

typedef struct ObjClosure {
  Obj object;
  ObjFunction *function;
  ObjUpvalue **upvalues;
  int upvalueCount; // always same as function->upvalueCount
} ObjClosure;

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct ObjNative {
  Obj object;
  NativeFn function;
  ObjString *name;
} ObjNative;

typedef struct ObjClass ObjClass;
typedef struct ObjClass {

  // NOTE: same fields, in same order, as instance. Can be cast to an
  // instance.
  Obj object;
  ObjClass *klass;
  ObjClass *singletonKlass;
  Table fields;
  Table hiddenFields;

  ObjString *name;
  ObjClass *superclass;
  Table methods;
  Table getters;
  Table setters;
} ObjClass;

extern ObjClass *lxObjClass;
extern ObjClass *lxClassClass;
extern ObjClass *lxAryClass;
extern ObjClass *lxMapClass;
extern ObjClass *lxErrClass;
extern ObjClass *lxArgErrClass;
extern Value lxLoadPath;

typedef struct ObjInstance {
  Obj object;
  ObjClass *klass;
  ObjClass *singletonKlass;
  Table fields;
  Table hiddenFields;
} ObjInstance;

typedef struct ObjBoundMethod {
  Obj object;
  Value receiver;
  Obj *callable; // ObjClosure* or ObjNative*
} ObjBoundMethod;

#define IS_STRING(value)        (isObjType(value, OBJ_T_STRING))
#define IS_FUNCTION(value)      (isObjType(value, OBJ_T_FUNCTION))
#define IS_CLOSURE(value)       (isObjType(value, OBJ_T_CLOSURE))
#define IS_NATIVE_FUNCTION(value) (isObjType(value, OBJ_T_NATIVE_FUNCTION))
#define IS_CLASS(value)         (isObjType(value, OBJ_T_CLASS))
#define IS_INSTANCE(value)      (isObjType(value, OBJ_T_INSTANCE))
#define IS_UPVALUE(value)       (isObjType(value, OBJ_T_UPVALUE))
#define IS_BOUND_METHOD(value)  (isObjType(value, OBJ_T_BOUND_METHOD))
#define IS_INTERNAL(value)      (isObjType(value, OBJ_T_INTERNAL))

#define IS_OBJ_STRING_FUNC (is_obj_string_p)
#define IS_STRING_FUNC (is_value_string_p)
#define IS_OBJ_FUNCTION_FUNC (is_obj_function_p)
#define IS_FUNCTION_FUNC (is_value_function_p)
#define IS_OBJ_CLOSURE_FUNC (is_obj_closure_p)
#define IS_CLOSURE_FUNC (is_value_closure_p)
#define IS_OBJ_NATIVE_FUNCTION_FUNC (is_obj_function_p)
#define IS_NATIVE_FUNCTION_FUNC (is_value_native_function_p)
#define IS_OBJ_CLASS_FUNC (is_obj_class_p)
#define IS_CLASS_FUNC (is_value_class_p)
#define IS_OBJ_INSTANCE_FUNC (is_obj_instance_p)
#define IS_INSTANCE_FUNC (is_value_instance_p)
#define IS_OBJ_BOUND_METHOD_FUNC (is_obj_bound_method_p)
#define IS_BOUND_METHOD_FUNC (is_value_bound_method_p)
#define IS_OBJ_UPVALUE_FUNC (is_obj_upvalue_p)
#define IS_UPVALUE_FUNC (is_value_upvalue_p)
#define IS_OBJ_INTERNAL_FUNC (is_obj_internal_p)
#define IS_INTERNAL_FUNC (is_value_internal_p)

#define IS_A(value,klass)       (IS_INSTANCE(value) && instanceIsA(AS_INSTANCE(value), klass))

#define IS_AN_ARRAY(value)      (IS_A(value, lxAryClass))
#define IS_T_ARRAY(value)       (IS_INSTANCE(value) && AS_INSTANCE(value)->klass == lxAryClass)
#define IS_A_MAP(value)         (IS_A(value, lxMapClass))
#define IS_T_MAP(value)         (IS_INSTANCE(value) && AS_INSTANCE(value)->klass == lxMapClass)
#define IS_AN_ERROR(value)      (IS_A(value, lxErrClass))

#define IS_SUBCLASS(subklass,superklass) (isSubclass(subklass,superklass))

#define AS_STRING(value)        ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)       (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value)      ((ObjFunction*)AS_OBJ(value))
#define AS_CLOSURE(value)       ((ObjClosure*)AS_OBJ(value))
#define AS_NATIVE_FUNCTION(value) ((ObjNative*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)  ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)         ((ObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value)      ((ObjInstance*)AS_OBJ(value))
#define AS_INTERNAL(value)      ((ObjInternal*)AS_OBJ(value))

#define ARRAY_GET(value, idx)    (arrayGet(value, idx))
#define ARRAY_SIZE(value)        (arraySize(value))
#define ARRAY_GETHIDDEN(value)   (arrayGetHidden(value))

#define LXARRAY_FOREACH(ary, el, idx) \
    for (idx = 0; idx < ARRAY_SIZE(ary) && \
        (el = ARRAY_GET(ary, idx)).type != VAL_T_SENTINEL; idx++)

#define MAP_GET(value, valkey)   (mapGet(value, valKey))
#define MAP_SIZE(value)          (mapSize(value))
#define MAP_GETHIDDEN(value)     (mapGetHidden(value))

typedef ObjString *(*newStringFunc)(char *chars, int length);
// String creation functions
ObjString *takeString(char *chars, int length); // uses provided memory as internal buffer, must be heap memory or will error when GC'ing the object
ObjString *copyString(char *chars, int length); // copies provided memory. Object lives on lox heap.
ObjString *hiddenString(char *chars, int length); // hidden from GC, used in tests mainly.
ObjString *newString(char *chars, int length); // always creates new string in vm.objects
void pushString(ObjString *a, ObjString *b); // always creates new string in vm.objects
ObjString *newStackString(char *chars, int length); // Used in native C functions. Object first lives in VM arena, conceptually.
ObjString *internedString(char *chars, int length); // Provided string must be interned by VM or will give error.
ObjString *dupString(ObjString *string);

void clearObjString(ObjString *str);

void objFreeze(Obj*);

// NOTE: don't call pushCString on a string value that's a key to a map! The
// hash value changes and the map won't be able to index it anymore.
void pushCString(ObjString *string, char *chars, int lenToAdd);
void pushCStringFmt(ObjString *string, const char *format, ...);
uint32_t hashString(char *key, int length);

Value       arrayGet(Value aryVal, int idx);
int         arraySize(Value aryVal);
void        arrayPush(Value aryVal, Value el);
ValueArray *arrayGetHidden(Value aryVal);
Value       newArray(void);

Value       newError(ObjClass *errClass, ObjString *msg);

Value       mapGet(Value mapVal, Value key);
Value       mapSize(Value mapVal);
Table      *mapGetHidden(Value mapVal);

void  setProp(Value self, ObjString *propName, Value val);
Value getProp(Value self, ObjString *propName);

// Object creation functions
ObjFunction *newFunction();
ObjClass *newClass(ObjString *name, ObjClass *superclass);
ObjInstance *newInstance(ObjClass *klass);
ObjNative *newNative(ObjString *name, NativeFn function);
ObjBoundMethod *newBoundMethod(ObjInstance *receiver, Obj *callable);
ObjInternal *newInternalObject(void *data, GCMarkFunc markFn, GCFreeFunc freeFn);
ObjClosure *newClosure(ObjFunction *function);
ObjUpvalue *newUpvalue(Value *slot);

void *internalGetData(ObjInternal *obj);

Obj *instanceFindMethod(ObjInstance *obj, ObjString *name);
bool instanceIsA(ObjInstance *inst, ObjClass *klass);
bool isSubclass(ObjClass *subklass, ObjClass *superklass);
Obj *classFindClassMethod(ObjClass *obj, ObjString *name);

ObjClass *classSingletonClass(ObjClass *klass);
ObjClass *instanceSingletonClass(ObjInstance *instance);

// Returns true if [value] is an object of type [type]. Do not call this
// directly, instead use the [IS_XXX] macro for the type in question.
static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

typedef bool (*obj_type_p)(Obj*);
bool is_obj_string_p(Obj*);
bool is_value_string_p(Value);
bool is_obj_function_p(Obj*);
bool is_value_function_p(Value);
bool is_obj_closure_p(Obj*);
bool is_value_closure_p(Value);
bool is_obj_native_function_p(Obj*);
bool is_value_native_function_p(Value);
bool is_obj_class_p(Obj*);
bool is_value_class_p(Value);
bool is_obj_instance_p(Obj*);
bool is_value_instance_p(Value);
bool is_obj_bound_method_p(Obj*);
bool is_value_bound_method_p(Value);
bool is_obj_upvalue_p(Obj*);
bool is_value_upvalue_p(Value);
bool is_obj_internal_p(Obj*);
bool is_value_internal_p(Value);

const char *typeOfObj(Obj *obj);

#endif
