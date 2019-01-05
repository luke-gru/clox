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
  OBJ_T_ARRAY,
  OBJ_T_INSTANCE, // includes Maps
  OBJ_T_CLASS,
  OBJ_T_MODULE,
  OBJ_T_FUNCTION,
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

typedef void (*GCMarkFunc)(Obj *obj);
typedef void (*GCFreeFunc)(Obj *obj);

typedef struct ObjInternal {
  Obj object;
  void *data; // internal data
  size_t dataSz;
  GCMarkFunc markFunc;
  GCMarkFunc freeFunc;
  bool isRealObject; // is allocated in object heap
} ObjInternal;

typedef struct sNode Node; // fwd decl
typedef struct Upvalue Upvalue; // fwd decl
typedef struct ObjFunction {
  Obj object;
  // NOTE: needs to be a value (non-pointer), as it's saved directly in the parent chunk as a constant value
  // and needs to be read by the VM
  Chunk *chunk;
  ObjString *name;
  Obj *klass; // ObjClass* or ObjModule* (if method)
  Node *funcNode;
  Upvalue *upvaluesInfo;
  unsigned short arity; // number of required args
  unsigned short numDefaultArgs; // number of optional default args
  unsigned short numKwargs;
  unsigned short upvalueCount;
  bool isSingletonMethod;
  bool hasRestArg;
  bool hasBlockArg;
  bool isBlock;
} ObjFunction;

typedef struct ObjUpvalue ObjUpvalue; // fwd decl
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
  bool isBlock;
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
extern ObjClass *lxBlockClass;
extern ObjClass *lxMutexClass;
extern ObjModule *lxGCModule;
extern ObjModule *lxProcessMod;
extern ObjClass *lxIOClass;
extern ObjClass *lxErrClass;
extern ObjClass *lxArgErrClass;
extern ObjClass *lxTypeErrClass;
extern ObjClass *lxNameErrClass;
extern ObjClass *lxSyntaxErrClass;
extern ObjClass *lxLoadErrClass;

extern ObjClass *lxBlockIterErrClass;
extern ObjClass *lxBreakBlockErrClass;
extern ObjClass *lxContinueBlockErrClass;
extern ObjClass *lxReturnBlockErrClass;

typedef struct ObjInstance {
  Obj object;
  ObjClass *klass;
  ObjClass *singletonKlass;
  Obj *finalizerFunc; // ObjClosure* or ObjNative*
  Table *fields;
  ObjInternal *internal;
} ObjInstance;

typedef struct ObjString {
    Obj object;
    ObjClass *klass;
    ObjClass *singletonKlass;
    Obj *finalizerFunc; // ObjClosure* or ObjNative*
    Table *fields;
    int length; // doesn't include NULL byte
    char *chars;
    uint32_t hash;
    int capacity;
    bool isStatic;
    bool isInterned;
    bool isShared;
} ObjString;

typedef struct ObjArray {
    Obj obj;
    ObjClass *klass;
    ObjClass *singletonKlass;
    Obj *finalizerFunc; // ObjClosure* or ObjNative*
    Table *fields;
    //union {
        ValueArray valAry;
        // TODO: add embed
    //} as;
} ObjArray;

extern ObjArray *lxLoadPath;
extern ObjArray *lxArgv;

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
        ObjArray array;
        ObjInstance instance;
        ObjClass klass;
        ObjModule module;
        ObjFunction function;
        ObjClosure closure;
        ObjNative native;
        ObjBoundMethod bound;
        ObjUpvalue upvalue;
        ObjInternal internal;
    } as;
} ObjAny;

// io/file internals
typedef struct LxFile {
    int fd;
    int mode;
    int oflags; // open flags
    bool isOpen;
    ObjString *name; // copied (owned value)
} LxFile;

#define IS_STRING(value)        (isObjType(value, OBJ_T_STRING))
#define IS_ARRAY(value)         (isObjType(value, OBJ_T_ARRAY))
#define IS_FUNCTION(value)      (isObjType(value, OBJ_T_FUNCTION))
#define IS_CLOSURE(value)       (isObjType(value, OBJ_T_CLOSURE))
#define IS_NATIVE_FUNCTION(value) (isObjType(value, OBJ_T_NATIVE_FUNCTION))
#define IS_CLASS(value)         (isObjType(value, OBJ_T_CLASS))
#define IS_MODULE(value)        (isObjType(value, OBJ_T_MODULE))
#define IS_INSTANCE(value)      (isObjType(value, OBJ_T_INSTANCE))
#define IS_INSTANCE_LIKE(value) (IS_INSTANCE(value) || IS_STRING(value) || IS_ARRAY(value) || IS_CLASS(value) || IS_MODULE(value))
#define IS_UPVALUE(value)       (isObjType(value, OBJ_T_UPVALUE))
#define IS_BOUND_METHOD(value)  (isObjType(value, OBJ_T_BOUND_METHOD))
#define IS_INTERNAL(value)      (isObjType(value, OBJ_T_INTERNAL))

#define IS_CLASS_FUNC (is_value_class_p)
#define IS_MODULE_FUNC (is_value_module_p)
#define IS_INSTANCE_FUNC (is_value_instance_p)
#define IS_CLOSURE_FUNC (is_value_closure_p)
#define IS_INSTANCE_OF_FUNC (is_value_instance_of_p)
#define IS_A_FUNC (is_value_a_p)

#define IS_A(value,klass)       ((IS_INSTANCE(value) || IS_ARRAY(value) || IS_STRING(value)) && instanceIsA(AS_INSTANCE(value), klass))

#define IS_A_MODULE(value)      (IS_A(value, lxModuleClass))
#define IS_AN_ARRAY(value)      (IS_A(value, lxAryClass))
#define IS_T_ARRAY(value)       (AS_INSTANCE(value)->klass == lxAryClass)
#define IS_A_MAP(value)         (IS_A(value, lxMapClass))
#define IS_T_MAP(value)         (IS_INSTANCE(value) && AS_INSTANCE(value)->klass == lxMapClass)
#define IS_AN_ERROR(value)      (IS_A(value, lxErrClass))
#define IS_A_THREAD(value)      (IS_A(value, lxThreadClass))
#define IS_A_STRING(value)      (IS_STRING(value) || IS_A(value, lxStringClass))

#define IS_SUBCLASS(subklass,superklass) (isSubclass(subklass,superklass))

#define AS_STRING(value)        ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)       (((ObjString*)AS_OBJ(value))->chars)
#define INSTANCE_AS_CSTRING(value) (AS_STRING(value)->chars)
#define VAL_TO_STRING(value)    (AS_STRING(value))
#define AS_FUNCTION(value)      ((ObjFunction*)AS_OBJ(value))
#define AS_CLOSURE(value)       ((ObjClosure*)AS_OBJ(value))
#define AS_NATIVE_FUNCTION(value) ((ObjNative*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)  ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)         ((ObjClass*)AS_OBJ(value))
#define AS_MODULE(value)        ((ObjModule*)AS_OBJ(value))
#define AS_INSTANCE(value)      ((ObjInstance*)AS_OBJ(value))
#define AS_ARRAY(value)         ((ObjArray*)AS_OBJ(value))
#define AS_INTERNAL(value)      ((ObjInternal*)AS_OBJ(value))

#define ARRAY_GET(value, idx)    (arrayGet(value, idx))
#define ARRAY_SIZE(value)        (arraySize(value))

#ifdef NAN_TAGGING
#define LXARRAY_FOREACH(ary, el, idx) \
    for (idx = 0; idx < ARRAY_SIZE(ary) && \
        (el = ARRAY_GET(ary, idx)) && !IS_UNDEF(el); idx++)

#define LXARRAY_FOREACH_REV(ary, el, idx) \
    for (idx = ARRAY_SIZE(ary)-1; idx >= 0 && \
        (el = ARRAY_GET(ary, idx)) && !IS_UNDEF(el); idx--)
#else
#define LXARRAY_FOREACH(ary, el, idx) \
    for (idx = 0; idx < ARRAY_SIZE(ary) && \
        (el = ARRAY_GET(ary, idx)).type != VAL_T_UNDEF; idx++)

#define LXARRAY_FOREACH_REV(ary, el, idx) \
    for (idx = ARRAY_SIZE(ary)-1; idx >= 0 && \
        (el = ARRAY_GET(ary, idx)).type != VAL_T_UNDEF; idx--)
#endif

#define MAP_GET(mapVal, valKey, pval)   (mapGet(mapVal, valKey, pval))
#define MAP_SIZE(mapVal)          (mapSize(mapVal))
#define MAP_GETHIDDEN(mapVal)     (mapGetHidden(mapVal))

#define FILE_GETHIDDEN(fileVal) (fileGetHidden(fileVal))
#define THREAD_GETHIDDEN(thVal) (threadGetHidden(thVal))

// strings (internal)
typedef ObjString *(*newStringFunc)(char *chars, int length);
// Strings as ObjString
ObjString *takeString(char *chars, int length); // uses provided memory as internal buffer, must be heap memory or will error when GC'ing the object
ObjString *copyString(char *chars, int length); // copies provided memory. Object lives on lox heap.
ObjString *hiddenString(char *chars, int length); // hidden from GC, used in tests mainly.
#define INTERN(chars) (internedString(chars, strlen(chars)))
ObjString *internedString(char *chars, int length);
ObjString *dupString(ObjString *string);
bool objStringEquals(ObjString *a, ObjString *b);

// strings as values
void clearString(Value self);
void pushString(Value self, Value pushed);
void stringInsertAt(Value self, Value insert, int at);
Value stringSubstr(Value self, int startIdx, int len);
Value stringIndexGet(Value self, int index);
Value stringIndexSet(Value self, int index, char c);
bool  stringEquals(Value a, Value b);

// NOTE: don't call pushCString on a string value that's a key to a map! The
// hash value changes and the map won't be able to index it anymore (see
// Map#rehash())
void pushCString(ObjString *string, char *chars, int lenToAdd);
void insertCString(ObjString *a, char *chars, int lenToAdd, int at);
void pushCStringFmt(ObjString *string, const char *format, ...);
void pushCStringVFmt(ObjString *string, const char *format, va_list ap);
uint32_t hashString(char *key, int length);

static inline void pushObjString(ObjString *a, ObjString *b) {
    pushCString(a, b->chars, b->length);
}
static inline void insertObjString(ObjString *a, ObjString *b, int at) {
    insertCString(a, b->chars, b->length, at);
}

// misc
void objFreeze(Obj*);
void objUnfreeze(Obj*);
static inline bool isFrozen(Obj *obj) {
    return obj->isFrozen;
}
void  setProp(Value self, ObjString *propName, Value val);
Value getProp(Value self, ObjString *propName);
static inline void *internalGetData(ObjInternal *obj) {
    return obj->data;
}
void setObjectFinalizer(ObjInstance *obj, Obj *callable);

// arrays
Value       newArray(void);
void        arrayPush(Value aryVal, Value el);
Value       arrayPop(Value aryVal);
void        arrayPushFront(Value aryVal, Value el);
Value       arrayPopFront(Value aryVal);
int         arrayDelete(Value aryVal, Value el);
void        arrayClear(Value aryVal);
bool        arrayEquals(Value self, Value other);
Value       arrayDup(Value other);
Value       newArrayConstant(void);
/**
 * NOTE: assumes idx is appropriate and within given range. See
 * ARRAY_SIZE(value)
 */
static inline Value arrayGet(Value aryVal, int idx) {
    ValueArray *ary = &AS_ARRAY(aryVal)->valAry;
    return ary->values[idx];
}

static inline int arraySize(Value aryVal) {
    ValueArray *ary = &AS_ARRAY(aryVal)->valAry;
    return ary->count;
}

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
Value newThread(void);
typedef struct LxThread LxThread; // fwd decl
LxThread *threadGetHidden(Value thread);

// blocks
Value newBlock(ObjClosure *closure);

// Object creation functions
ObjFunction *newFunction(Chunk *chunk, Node *funcNode);
ObjClass *newClass(ObjString *name, ObjClass *superclass);
ObjModule *newModule(ObjString *name);
ObjInstance *newInstance(ObjClass *klass);
ObjArray *allocateArray(ObjClass *klass);
ObjNative *newNative(ObjString *name, NativeFn function);
ObjBoundMethod *newBoundMethod(ObjInstance *receiver, Obj *callable);
ObjInternal *newInternalObject(bool isRealObject, void *data, size_t dataSz, GCMarkFunc markFn, GCFreeFunc freeFn);
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
char *className(ObjClass *klass);

static inline bool isCallable(Value val) {
    return IS_CLASS(val) || IS_NATIVE_FUNCTION(val) ||
        IS_BOUND_METHOD(val) || IS_CLOSURE(val);
}

bool is_value_class_p(Value);
bool is_value_module_p(Value);
bool is_value_instance_p(Value);
bool is_value_closure_p(Value val);
bool is_value_instance_of_p(Value, ObjClass*);
bool is_value_a_p(Value, ObjClass*);

#endif
