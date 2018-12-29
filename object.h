#ifndef clox_object_h
#define clox_object_h

#include <pthread.h>
#include "common.h"
#include "chunk.h"
#include "value.h"
#include "table.h"

typedef enum ObjType {
  OBJ_T_NONE = 0, // when object is unitialized or freed
  OBJ_T_STRING,   // internal string value only, Strings in lox are instances
  OBJ_T_FUNCTION,
  OBJ_T_INSTANCE, // includes Strings, Arrays, Maps
  OBJ_T_CLASS,
  OBJ_T_MODULE,
  OBJ_T_NATIVE_FUNCTION,
  OBJ_T_BOUND_METHOD,
  OBJ_T_UPVALUE,
  OBJ_T_CLOSURE,
  OBJ_T_INTERNAL,
  OBJ_T_LAST, // should never happen
} ObjType;

typedef struct ObjAny ObjAny;

// basic object structure that all objects (values of VAL_T_OBJ type)
typedef struct Obj {
  ObjType type; // redundant, but we need for now
  ObjAny *nextFree;
  size_t objectId;
  // GC fields
  unsigned short GCGen;
  bool isDark; // is this object marked?
  bool noGC; // don't collect this object
  // Other fields
  bool isFrozen;
} Obj;

typedef struct ObjString {
  Obj object;
  int length; // doesn't include NULL byte
  char *chars;
  uint32_t hash;
  int capacity;
  bool isStatic;
  bool isInterned;
} ObjString;

typedef void (*GCMarkFunc)(Obj *obj);
typedef void (*GCFreeFunc)(Obj *obj);

typedef struct ObjInternal {
  Obj object;
  void *data; // internal data
  size_t dataSz;
  GCMarkFunc markFunc;
  GCMarkFunc freeFunc;
} ObjInternal;

typedef struct sNode Node; // fwd decl
typedef struct ObjFunction {
  Obj object;
  // NOTE: needs to be a value (non-pointer), as it's saved directly in the parent chunk as a constant value
  // and needs to be read by the VM
  Chunk *chunk;
  ObjString *name;
  Obj *klass; // ObjClass* or ObjModule* (if method)
  Node *funcNode;
  unsigned short arity; // number of required args
  unsigned short numDefaultArgs; // number of optional default args
  unsigned short numKwargs;
  unsigned short upvalueCount;
  bool isSingletonMethod;
  bool hasRestArg;
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

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct ObjNative {
  Obj object;
  NativeFn function;
  ObjString *name;
  Obj *klass; // class or module, if a method
  bool isStatic; // if static method
} ObjNative;

typedef struct ObjClass ObjClass; // fwd decl
typedef struct ObjModule ObjModule; // fwd decl

#define CLASSINFO(klass) (((ObjClass*) (klass))->classInfo)
typedef struct ClassInfo {
  Table *methods;
  Table *getters;
  Table *setters;
  ObjString *name;
  // for classes only
  ObjClass *superclass;
  vec_void_t v_includedMods; // pointers to ObjModule
  Obj *singletonOf;  // if singleton class
} ClassInfo;

typedef struct ObjClass {

  // NOTE: same fields, in same order, as ObjInstance. Can be cast to an
  // ObjInstance. Also, can be cast to an ObjModule.
  Obj object;
  ObjClass *klass; // always lxClassClass
  ObjClass *singletonKlass;
  Obj *finalizerFunc;
  Table *fields;
  Table *hiddenFields;

  ClassInfo *classInfo;
} ObjClass;

typedef struct ObjModule {

  // NOTE: same fields, in same order, as ObjInstance. Can be cast to an
  // ObjInstance, as well as an ObjClass.
  Obj object;
  ObjClass *klass; // always lxModuleClass
  ObjClass *singletonKlass;
  Obj *finalizerFunc;
  Table *fields;
  Table *hiddenFields;

  ClassInfo *classInfo;
} ObjModule;

extern ObjClass *lxObjClass;
extern ObjClass *lxStringClass;
extern ObjClass *lxClassClass;
extern ObjClass *lxModuleClass;
extern ObjClass *lxAryClass;
extern ObjClass *lxMapClass;
extern ObjClass *lxIteratorClass;
extern ObjClass *lxFileClass;
extern ObjClass *lxThreadClass;
extern ObjModule *lxGCModule;
extern ObjModule *lxProcessMod;
extern ObjModule *lxIOMod;
extern ObjClass *lxErrClass;
extern ObjClass *lxArgErrClass;
extern ObjClass *lxTypeErrClass;
extern ObjClass *lxNameErrClass;
extern ObjClass *lxSyntaxErrClass;
extern ObjClass *lxLoadErrClass;

extern Value lxLoadPath;
extern Value lxArgv;

typedef struct ObjInstance {
  Obj object;
  ObjClass *klass;
  ObjClass *singletonKlass;
  Obj *finalizerFunc; // ObjClosure* or ObjNative*
  Table *fields;
  Table *hiddenFields;
} ObjInstance;

typedef struct ObjBoundMethod {
  Obj object;
  Value receiver;
  Obj *callable; // ObjClosure* or ObjNative*
} ObjBoundMethod;

// is big enough to represent any object
typedef struct ObjAny {
    union {
        Obj basic;
        ObjString string;
        ObjClass klass;
        ObjModule module;
        ObjFunction function;
        ObjClosure closure;
        ObjNative native;
        ObjInstance instance;
        ObjBoundMethod bound;
        ObjUpvalue upvalue;
        ObjInternal internal;
    } as;
} ObjAny;

// thread internals
typedef enum ThreadStatus {
    THREAD_STOPPED = 0,
    THREAD_RUNNING,
    THREAD_ZOMBIE
} ThreadStatus;

typedef struct LxThread {
    pthread_t tid;
    ThreadStatus status;
} LxThread;

// file internals
typedef struct LxFile {
    int fd;
    int oflags; // open flags
    bool isOpen;
    ObjString *name; // copied (owned value)
} LxFile;

#define IS_STRING(value)        (isObjType(value, OBJ_T_STRING))
#define IS_FUNCTION(value)      (isObjType(value, OBJ_T_FUNCTION))
#define IS_CLOSURE(value)       (isObjType(value, OBJ_T_CLOSURE))
#define IS_NATIVE_FUNCTION(value) (isObjType(value, OBJ_T_NATIVE_FUNCTION))
#define IS_CLASS(value)         (isObjType(value, OBJ_T_CLASS))
#define IS_MODULE(value)        (isObjType(value, OBJ_T_MODULE))
#define IS_INSTANCE(value)      (isObjType(value, OBJ_T_INSTANCE))
#define IS_INSTANCE_LIKE(value) (IS_INSTANCE(value) || IS_CLASS(value) || IS_MODULE(value))
#define IS_UPVALUE(value)       (isObjType(value, OBJ_T_UPVALUE))
#define IS_BOUND_METHOD(value)  (isObjType(value, OBJ_T_BOUND_METHOD))
#define IS_INTERNAL(value)      (isObjType(value, OBJ_T_INTERNAL))

#define IS_OBJ_FUNCTION_FUNC (is_obj_function_p)
#define IS_FUNCTION_FUNC (is_value_function_p)
#define IS_OBJ_CLOSURE_FUNC (is_obj_closure_p)
#define IS_CLOSURE_FUNC (is_value_closure_p)
#define IS_OBJ_NATIVE_FUNCTION_FUNC (is_obj_function_p)
#define IS_NATIVE_FUNCTION_FUNC (is_value_native_function_p)
#define IS_OBJ_CLASS_FUNC (is_obj_class_p)
#define IS_CLASS_FUNC (is_value_class_p)
#define IS_OBJ_MODULE_FUNC (is_obj_module_p)
#define IS_MODULE_FUNC (is_value_module_p)
#define IS_OBJ_INSTANCE_FUNC (is_obj_instance_p)
#define IS_INSTANCE_FUNC (is_value_instance_p)
#define IS_OBJ_BOUND_METHOD_FUNC (is_obj_bound_method_p)
#define IS_BOUND_METHOD_FUNC (is_value_bound_method_p)
#define IS_OBJ_UPVALUE_FUNC (is_obj_upvalue_p)
#define IS_UPVALUE_FUNC (is_value_upvalue_p)
#define IS_OBJ_INTERNAL_FUNC (is_obj_internal_p)
#define IS_INTERNAL_FUNC (is_value_internal_p)
#define IS_OBJ_INSTANCE_OF_FUNC (is_obj_instance_of_p)
#define IS_INSTANCE_OF_FUNC (is_value_instance_of_p)
#define IS_OBJ_A_FUNC (is_obj_a_p)
#define IS_A_FUNC (is_value_a_p)

#define IS_A(value,klass)       (IS_INSTANCE(value) && instanceIsA(AS_INSTANCE(value), klass))

#define IS_A_MODULE(value)      (IS_A(value, lxModuleClass))
#define IS_AN_ARRAY(value)      (IS_A(value, lxAryClass))
#define IS_T_ARRAY(value)       (IS_INSTANCE(value) && AS_INSTANCE(value)->klass == lxAryClass)
#define IS_A_MAP(value)         (IS_A(value, lxMapClass))
#define IS_T_MAP(value)         (IS_INSTANCE(value) && AS_INSTANCE(value)->klass == lxMapClass)
#define IS_A_STRING(value)      (IS_A(value, lxStringClass))
#define IS_T_STRING(value)      (IS_INSTANCE(value) && AS_INSTANCE(value)->klass == lxStringClass)
#define IS_AN_ERROR(value)      (IS_A(value, lxErrClass))
#define IS_A_THREAD(value)      (IS_A(value, lxThreadClass))

#define IS_SUBCLASS(subklass,superklass) (isSubclass(subklass,superklass))

#define AS_STRING(value)        ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)       (((ObjString*)AS_OBJ(value))->chars)
#define INSTANCE_AS_CSTRING(value) (STRING_GETHIDDEN(value)->chars)
#define VAL_TO_STRING(value)    (IS_A_STRING(value) ? STRING_GETHIDDEN(value) : AS_STRING(value))
#define AS_FUNCTION(value)      ((ObjFunction*)AS_OBJ(value))
#define AS_CLOSURE(value)       ((ObjClosure*)AS_OBJ(value))
#define AS_NATIVE_FUNCTION(value) ((ObjNative*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)  ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)         ((ObjClass*)AS_OBJ(value))
#define AS_MODULE(value)        ((ObjModule*)AS_OBJ(value))
#define AS_INSTANCE(value)      ((ObjInstance*)AS_OBJ(value))
#define AS_INTERNAL(value)      ((ObjInternal*)AS_OBJ(value))

#define ARRAY_GET(value, idx)    (arrayGet(value, idx))
#define ARRAY_SIZE(value)        (arraySize(value))
#define ARRAY_GETHIDDEN(value)   (arrayGetHidden(value))

#define LXARRAY_FOREACH(ary, el, idx) \
    for (idx = 0; idx < ARRAY_SIZE(ary) && \
        (el = ARRAY_GET(ary, idx)).type != VAL_T_UNDEF; idx++)

#define LXARRAY_FOREACH_REV(ary, el, idx) \
    for (idx = ARRAY_SIZE(ary)-1; idx >= 0 && \
        (el = ARRAY_GET(ary, idx)).type != VAL_T_UNDEF; idx--)

#define MAP_GET(mapVal, valKey, pval)   (mapGet(mapVal, valKey, pval))
#define MAP_SIZE(mapVal)          (mapSize(mapVal))
#define MAP_GETHIDDEN(mapVal)     (mapGetHidden(mapVal))

#define STRING_GETHIDDEN(stringVal) (stringGetHidden(stringVal))
#define FILE_GETHIDDEN(fileVal) (fileGetHidden(fileVal))

// strings (internal)
typedef ObjString *(*newStringFunc)(char *chars, int length);
// String creation functions
ObjString *takeString(char *chars, int length); // uses provided memory as internal buffer, must be heap memory or will error when GC'ing the object
ObjString *copyString(char *chars, int length); // copies provided memory. Object lives on lox heap.
ObjString *hiddenString(char *chars, int length); // hidden from GC, used in tests mainly.
ObjString *internedString(char *chars, int length); // Provided string must be interned by VM or will give error.
ObjString *dupString(ObjString *string);
void pushObjString(ObjString *a, ObjString *b);
void clearObjString(ObjString *str);
void insertObjString(ObjString *a, ObjString *b, int at);
bool objStringEquals(ObjString *a, ObjString *b);

// string instances
Value dupStringInstance(Value instance);
Value newStringInstance(ObjString *buf);
void clearString(Value self);
void pushString(Value self, Value pushed);
void stringInsertAt(Value self, Value insert, int at);
Value stringSubstr(Value self, int startIdx, int len);
Value stringIndexGet(Value self, int index);
Value stringIndexSet(Value self, int index, char c);
bool  stringEquals(Value a, Value b);
ObjString *stringGetHidden(Value instance);

// NOTE: don't call pushCString on a string value that's a key to a map! The
// hash value changes and the map won't be able to index it anymore.
void pushCString(ObjString *string, char *chars, int lenToAdd);
void insertCString(ObjString *a, char *chars, int lenToAdd, int at);
void pushCStringFmt(ObjString *string, const char *format, ...);
void pushCStringVFmt(ObjString *string, const char *format, va_list ap);
uint32_t hashString(char *key, int length);

// misc
void objFreeze(Obj*);
void objUnfreeze(Obj*);
bool isFrozen(Obj*);
void  setProp(Value self, ObjString *propName, Value val);
Value getProp(Value self, ObjString *propName);
Value getHiddenProp(Value self, ObjString *propName);
void *internalGetData(ObjInternal *obj);
void setObjectFinalizer(ObjInstance *obj, Obj *callable);

// arrays
Value       newArray(void);
Value       arrayGet(Value aryVal, int idx);
int         arraySize(Value aryVal);
void        arrayPush(Value aryVal, Value el);
Value       arrayPop(Value aryVal);
void        arrayPushFront(Value aryVal, Value el);
Value       arrayPopFront(Value aryVal);
int         arrayDelete(Value aryVal, Value el);
void        arrayClear(Value aryVal);
bool        arrayEquals(Value self, Value other);
ValueArray *arrayGetHidden(Value aryVal);

// errors
Value       newError(ObjClass *errClass, Value msg);

// maps
Value       newMap(void);
bool        mapGet(Value map, Value key, Value *val);
void        mapSet(Value map, Value key, Value val);
Value       mapSize(Value map);
void        mapClear(Value map);
bool        mapEquals(Value map, Value other);
Table      *mapGetHidden(Value map);

// threads
void threadSetStatus(Value thread, ThreadStatus status);
void threadSetId(Value thread, pthread_t tid);
ThreadStatus threadGetStatus(Value thread);
pthread_t threadGetId(Value thread);
Value newThread(void);
LxThread *threadGetInternal(Value thread);

// IO
size_t IOWrite(int fd, const void *buf, size_t count);

// files
LxFile *fileGetInternal(Value file);
void fileClose(Value file);

// Object creation functions
ObjFunction *newFunction(Chunk *chunk, Node *funcNode);
ObjClass *newClass(ObjString *name, ObjClass *superclass);
ObjModule *newModule(ObjString *name);
ObjInstance *newInstance(ObjClass *klass);
ObjNative *newNative(ObjString *name, NativeFn function);
ObjBoundMethod *newBoundMethod(ObjInstance *receiver, Obj *callable);
ObjInternal *newInternalObject(void *data, size_t dataSz, GCMarkFunc markFn, GCFreeFunc freeFn);
ObjClosure *newClosure(ObjFunction *function);
ObjUpvalue *newUpvalue(Value *slot);

// Object destruction functions
void freeClassInfo(ClassInfo *cinfo);

// methods/classes
Obj *instanceFindMethod(ObjInstance *obj, ObjString *name);
Obj *instanceFindMethodOrRaise(ObjInstance *obj, ObjString *name);
Obj *instanceFindGetter(ObjInstance *obj, ObjString *name);
Obj *instanceFindSetter(ObjInstance *obj, ObjString *name);
Obj *classFindStaticMethod(ObjClass *obj, ObjString *name);
Obj *moduleFindStaticMethod(ObjModule *obj, ObjString *name);
bool instanceIsA(ObjInstance *inst, ObjClass *klass);
bool isSubclass(ObjClass *subklass, ObjClass *superklass);
const char *instanceClassName(ObjInstance *obj);

// metaclasses
ObjClass *singletonClass(Obj *obj);
ObjClass *classSingletonClass(ObjClass *klass);
ObjClass *moduleSingletonClass(ObjModule *mod);
ObjClass *instanceSingletonClass(ObjInstance *instance);

// other
// Returns true if [value] is an object of type [type]. Do not call this
// directly, instead use the [IS_XXX] macro for the type in question.
static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}
const char *typeOfObj(Obj *obj);
bool isInstanceLikeObj(Obj *obj);
size_t sizeofObjType(ObjType type);
const char *objTypeName(ObjType type);


typedef bool (*obj_type_p)(Obj*);
bool is_obj_function_p(Obj*);
bool is_value_function_p(Value);
bool is_obj_closure_p(Obj*);
bool is_value_closure_p(Value);
bool is_obj_native_function_p(Obj*);
bool is_value_native_function_p(Value);
bool is_obj_class_p(Obj*);
bool is_value_class_p(Value);
bool is_obj_module_p(Obj*);
bool is_value_module_p(Value);
bool is_obj_instance_p(Obj*);
bool is_value_instance_p(Value);
bool is_obj_bound_method_p(Obj*);
bool is_value_bound_method_p(Value);
bool is_obj_upvalue_p(Obj*);
bool is_value_upvalue_p(Value);
bool is_obj_internal_p(Obj*);
bool is_value_internal_p(Value);

bool is_obj_instance_of_p(Obj*, ObjClass*);
bool is_value_instance_of_p(Value, ObjClass*);
bool is_obj_a_p(Obj*, ObjClass*);
bool is_value_a_p(Value, ObjClass*);

#endif
