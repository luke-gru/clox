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
  OBJ_T_MAP,
  OBJ_T_INSTANCE, // includes Maps
  OBJ_T_CLASS,
  OBJ_T_MODULE,
  OBJ_T_ICLASS, // included module
  OBJ_T_FUNCTION,
  OBJ_T_NATIVE_FUNCTION,
  OBJ_T_BOUND_METHOD,
  OBJ_T_UPVALUE,
  OBJ_T_CLOSURE,
  OBJ_T_INTERNAL,
  OBJ_T_LAST, // should never happen
} ObjType;

typedef struct ObjAny ObjAny;

// common flags
#define OBJ_FLAG_NONE (0)
#define OBJ_FLAG_DARK (1 << 0)
#define OBJ_FLAG_HAS_FINALIZER (1 << 1)
#define OBJ_FLAG_FROZEN (1 << 2)
#define OBJ_FLAG_NOGC (1 << 3)
#define OBJ_FLAG_PUSHED_VM_STACK (1 << 4)
// flags that may or may not be used by certain types
#define OBJ_FLAG_USER1 (1 << 10)
#define OBJ_FLAG_USER2 (1 << 11)
#define OBJ_FLAG_USER3 (1 << 12)

#define OBJ_HAS_FLAG(obj, name) ((((Obj*)obj)->flags & OBJ_FLAG_##name) != 0)
#define OBJ_SET_FLAG(obj, name) (((Obj*)obj)->flags |= OBJ_FLAG_##name)
#define OBJ_UNSET_FLAG(obj, name) (((Obj*)obj)->flags &= (~OBJ_FLAG_##name))

#define OBJ_IS_DARK(obj) OBJ_HAS_FLAG(obj, DARK)
#define OBJ_SET_DARK(obj) OBJ_SET_FLAG(obj, DARK)
#define OBJ_UNSET_DARK(obj) OBJ_UNSET_FLAG(obj, DARK)
#define OBJ_HAS_FINALIZER(obj) OBJ_HAS_FLAG(obj, HAS_FINALIZER)
#define OBJ_SET_HAS_FINALIZER(obj) OBJ_SET_FLAG(obj, HAS_FINALIZER)
#define OBJ_UNSET_HAS_FINALIZER(obj) OBJ_UNSET_FLAG(obj, HAS_FINALIZER)
#define OBJ_IS_FROZEN(obj) OBJ_HAS_FLAG(obj, FROZEN)
#define OBJ_SET_FROZEN(obj) OBJ_SET_FLAG(obj, FROZEN)
#define OBJ_UNSET_FROZEN(obj) OBJ_UNSET_FLAG(obj, FROZEN)
#define OBJ_IS_HIDDEN(obj) OBJ_HAS_FLAG(obj, NOGC)
#define OBJ_SET_HIDDEN(obj) OBJ_SET_FLAG(obj, NOGC)
#define OBJ_UNSET_HIDDEN(obj) OBJ_UNSET_FLAG(obj, NOGC)
#define OBJ_IS_PUSHED_VM_STACK(obj) OBJ_HAS_FLAG(obj, PUSHED_VM_STACK)
#define OBJ_SET_PUSHED_VM_STACK(obj) OBJ_SET_FLAG(obj, PUSHED_VM_STACK)
#define OBJ_UNSET_PUSHED_VM_STACK(obj) OBJ_UNSET_FLAG(obj, PUSHED_VM_STACK)

#define OBJ_HAS_USER1_FLAG(obj) OBJ_HAS_FLAG(obj, USER1)
#define OBJ_SET_USER1_FLAG(obj) OBJ_SET_FLAG(obj, USER1)
#define OBJ_UNSET_USER1_FLAG(obj) OBJ_UNSET_FLAG(obj, USER1)
#define OBJ_HAS_USER2_FLAG(obj) OBJ_HAS_FLAG(obj, USER2)
#define OBJ_SET_USER2_FLAG(obj) OBJ_SET_FLAG(obj, USER2)
#define OBJ_UNSET_USER2_FLAG(obj) OBJ_UNSET_FLAG(obj, USER2)
#define OBJ_HAS_USER3_FLAG(obj) OBJ_HAS_FLAG(obj, USER3)
#define OBJ_SET_USER3_FLAG(obj) OBJ_SET_FLAG(obj, USER3)
#define OBJ_UNSET_USER3_FLAG(obj) OBJ_UNSET_FLAG(obj, USER3)

// basic object structure that all objects (values of VAL_T_OBJ type)
typedef struct Obj {
    ObjType type;
    uint16_t flags;
    ObjAny *nextFree;
    size_t objectId;
    // GC fields
    unsigned short GCGen;
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
  Obj *superclass; // ObjClass or ObjIClass
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

#define CLASS_SUPER(klass)      (((Obj*)klass)->type == OBJ_T_CLASS ? CLASSINFO(klass)->superclass : ((ObjIClass*) klass)->superklass)
#define CLASS_METHOD_TBL(klass) ((klass)->type == OBJ_T_CLASS ? CLASSINFO((klass))->methods : CLASSINFO(((ObjIClass*) (klass))->mod)->methods)
#define CLASS_GETTER_TBL(klass) ((klass)->type == OBJ_T_CLASS ? CLASSINFO((klass))->getters : CLASSINFO(((ObjIClass*) (klass))->mod)->getters)
#define CLASS_SETTER_TBL(klass) ((klass)->type == OBJ_T_CLASS ? CLASSINFO((klass))->setters : CLASSINFO(((ObjIClass*) (klass))->mod)->setters)

// included module "class"
typedef struct ObjIClass {
    Obj object;
    ObjClass *klass;
    ObjModule *mod;
    Obj *superklass; // ObjClass or ObjIClass
    bool isSetup;
} ObjIClass;

typedef struct ObjInstance {
  Obj object;
  ObjClass *klass;
  ObjClass *singletonKlass;
  Obj *finalizerFunc; // ObjClosure* or ObjNative*
  Table *fields;
  ObjInternal *internal;
} ObjInstance;

#define STRING_FLAG_STATIC OBJ_FLAG_USER1
#define STRING_FLAG_INTERNED OBJ_FLAG_USER2
#define STRING_FLAG_SHARED OBJ_FLAG_USER3

#define STRING_IS_STATIC OBJ_HAS_USER1_FLAG
#define STRING_SET_STATIC OBJ_SET_USER1_FLAG
#define STRING_UNSET_STATIC OBJ_UNSET_USER1_FLAG

#define STRING_IS_INTERNED OBJ_HAS_USER2_FLAG
#define STRING_SET_INTERNED OBJ_SET_USER2_FLAG
#define STRING_UNSET_INTERNED OBJ_UNSET_USER2_FLAG

#define STRING_IS_SHARED OBJ_HAS_USER3_FLAG
#define STRING_SET_SHARED OBJ_SET_USER3_FLAG
#define STRING_UNSET_SHARED OBJ_UNSET_USER3_FLAG
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
} ObjString;

#define ARRAY_FLAG_SHARED OBJ_FLAG_USER1
#define ARRAY_FLAG_STATIC OBJ_FLAG_USER2
#define ARRAY_IS_SHARED OBJ_HAS_USER1_FLAG
#define ARRAY_SET_SHARED OBJ_SET_USER1_FLAG
#define ARRAY_UNSET_SHARED OBJ_UNSET_USER1_FLAG
#define ARRAY_IS_STATIC OBJ_HAS_USER2_FLAG
#define ARRAY_SET_STATIC OBJ_SET_USER2_FLAG
#define ARRAY_UNSET_STATIC OBJ_UNSET_USER2_FLAG
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

typedef struct ObjMap {
    Obj obj;
    ObjClass *klass;
    ObjClass *singletonKlass;
    Obj *finalizerFunc; // ObjClosure* or ObjNative*
    Table *fields;
    //union {
        Table *table;
        // TODO: add embed
    //} as;
} ObjMap;

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
        ObjMap map;
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


// is the value an instance of this type, no subtype check
#define IS_STRING(value)        (isObjType(value, OBJ_T_STRING))
#define IS_ARRAY(value)         (isObjType(value, OBJ_T_ARRAY))
#define IS_MAP(value)           (isObjType(value, OBJ_T_MAP))
#define IS_FUNCTION(value)      (isObjType(value, OBJ_T_FUNCTION))
#define IS_CLOSURE(value)       (isObjType(value, OBJ_T_CLOSURE))
#define IS_NATIVE_FUNCTION(value) (isObjType(value, OBJ_T_NATIVE_FUNCTION))
#define IS_CLASS(value)         (isObjType(value, OBJ_T_CLASS))
#define IS_MODULE(value)        (isObjType(value, OBJ_T_MODULE))
#define IS_INSTANCE(value)      (isObjType(value, OBJ_T_INSTANCE))
#define IS_INSTANCE_LIKE(value) (IS_INSTANCE(value) || IS_STRING(value) || IS_ARRAY(value) || IS_MAP(value) || IS_CLASS(value) || IS_MODULE(value))
#define IS_UPVALUE(value)       (isObjType(value, OBJ_T_UPVALUE))
#define IS_BOUND_METHOD(value)  (isObjType(value, OBJ_T_BOUND_METHOD))
#define IS_INTERNAL(value)      (isObjType(value, OBJ_T_INTERNAL))

#define IS_CLASS_FUNC (is_value_class_p)
#define IS_MODULE_FUNC (is_value_module_p)
#define IS_INSTANCE_FUNC (is_value_instance_p)
#define IS_CLOSURE_FUNC (is_value_closure_p)
#define IS_INSTANCE_OF_FUNC (is_value_instance_of_p)
#define IS_A_FUNC (is_value_a_p)

#define IS_A(value, klass)      ((IS_INSTANCE(value) || IS_ARRAY(value) || IS_STRING(value) || IS_MAP(value)) && instanceIsA(AS_INSTANCE(value), klass))

#define IS_A_MODULE(value)      (IS_A(value, lxModuleClass))
#define IS_AN_ARRAY(value)      (IS_A(value, lxAryClass))
#define IS_T_ARRAY(value)       (AS_INSTANCE(value)->klass == lxAryClass)
#define IS_A_MAP(value)         (IS_A(value, lxMapClass))
#define IS_T_MAP(value)         (IS_MAP(value))
#define IS_AN_ERROR(value)      (IS_A(value, lxErrClass))
#define IS_A_THREAD(value)      (IS_A(value, lxThreadClass))
#define IS_A_STRING(value)      (IS_STRING(value))
#define IS_A_BLOCK(value)       (IS_INSTANCE(value) && IS_A(value, lxBlockClass))

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
#define AS_MAP(value)           ((ObjMap*)AS_OBJ(value))

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

#define MAP_GET(mapVal, valKey, pval) (mapGet(mapVal, valKey, pval))
#define MAP_SIZE(mapVal)          (mapSize(mapVal))

#define FILE_GETHIDDEN(fileVal) (fileGetHidden(fileVal))
#define THREAD_GETHIDDEN(thVal) (threadGetHidden(thVal))

// object creation flags
#define NEWOBJ_FLAG_NONE 0
#define NEWOBJ_FLAG_OLD 1
#define NEWOBJ_FLAG_FROZEN 2
#define NEWOBJ_FLAG_HIDDEN 4

// strings (internal)
typedef ObjString *(*newStringFunc)(char *chars, int length, int flags);
// Strings as ObjString
ObjString *takeString(char *chars, int length, int flags); // uses provided memory as internal buffer, must be heap memory or will error when GC'ing the object
ObjString *copyString(char *chars, int length, int flags); // copies provided memory. Object lives on lox heap.
ObjString *hiddenString(char *chars, int length, int flags); // hidden from GC, used in tests mainly.
#define INTERN(chars) (internedString(chars, strlen(chars), NEWOBJ_FLAG_NONE))
#define INTERNED(chars, len) (internedString(chars, len, NEWOBJ_FLAG_NONE))
ObjString *internedString(char *chars, int length, int flags);
bool objStringEquals(ObjString *a, ObjString *b);
static inline ObjString *dupString(ObjString *string) {
    return copyString(string->chars, string->length, NEWOBJ_FLAG_NONE);
}

// strings as values
void clearString(Value self);
void pushString(Value self, Value pushed);
void stringInsertAt(Value self, Value insert, int at);
Value stringSubstr(Value self, int startIdx, int len);
Value stringIndexGet(Value self, int index);
Value stringIndexSet(Value self, int index, char c);
bool stringEquals(Value a, Value b);

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
    return OBJ_IS_FROZEN(obj);
}
void  setProp(Value self, ObjString *propName, Value val);
Value getProp(Value self, ObjString *propName);
static inline void *internalGetData(ObjInternal *obj) {
    return obj->data;
}
void setObjectFinalizer(ObjInstance *obj, Obj *callable);

// arrays
Value  newArray(void);
void   arrayPush(Value aryVal, Value el);
Value  arrayPop(Value aryVal);
void   arrayPushFront(Value aryVal, Value el);
Value  arrayPopFront(Value aryVal);
int    arrayDelete(Value aryVal, Value el);
void   arrayClear(Value aryVal);
bool   arrayEquals(Value self, Value other);
Value  arrayDup(Value other);
Value  newArrayConstant(void);
static inline void arrayDedup(ObjArray *ary) {
    if (ARRAY_IS_SHARED(ary)) {
        ValueArray newValAry;
        initValueArray(&newValAry);
        ValueArray valAry = ary->valAry;
        for (int i = 0; i < valAry.count; i++) {
            writeValueArrayEnd(&newValAry, valAry.values[i]);
        }
        ary->valAry = newValAry;
        ARRAY_UNSET_SHARED(ary);
    }
}
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
Value newError(ObjClass *errClass, Value msg);

// maps
Value newMap(void);
Value  newMapConstant(void);
bool mapEquals(Value map, Value other);
static inline Table *mapGetHidden(Value mapVal) {
    ObjInstance *inst = AS_INSTANCE(mapVal);
    return (Table*)inst->internal->data;
}
static inline bool mapGet(Value mapVal, Value key, Value *ret) {
    Table *map = AS_MAP(mapVal)->table;
    if (tableGet(map, key, ret)) {
        return true;
    } else {
        return false;
    }
}
// NOTE: doesn't check frozenness or type of `mapVal`
static inline void mapSet(Value mapVal, Value key, Value val) {
    Table *map = AS_MAP(mapVal)->table;
    tableSet(map, key, val);
}

// number of key-value pairs
static inline Value mapSize(Value mapVal) {
    Table *map = AS_MAP(mapVal)->table;
    return NUMBER_VAL(map->count);
}

// NOTE: doesn't check frozenness or type of `mapVal`
static inline void mapClear(Value mapVal) {
    Table *map = AS_MAP(mapVal)->table;
    freeTable(map);
}

// threads
Value newThread(void);
typedef struct LxThread LxThread; // fwd decl
static inline LxThread *threadGetHidden(Value thread) {
    ObjInternal *i = AS_INSTANCE(thread)->internal;
    return (LxThread*)i->data;
}

Value mapDup(Value other);

// blocks
Value newBlock(Obj *callable);
Obj *blockCallable(Value blk); // returns Closure/ObjNative
Obj *blockCallableBlock(Value blk); // returns Function/ObjNative
typedef struct CallFrame CallFrame; // fwd decl
ObjInstance *getBlockArg(CallFrame *frame);

// Object creation functions
ObjFunction *newFunction(Chunk *chunk, Node *funcNode, int flags);
ObjClass *newClass(ObjString *name, ObjClass *superclass, int flags);
ObjModule *newModule(ObjString *name, int flags);
ObjIClass *newIClass(ObjClass *klass, ObjModule *mod, int flags);
void setupIClass(ObjIClass *iclass);
ObjInstance *newInstance(ObjClass *klass, int flags);
ObjArray *allocateArray(ObjClass *klass, int flags);
ObjNative *newNative(ObjString *name, NativeFn function, int flags);
ObjBoundMethod *newBoundMethod(ObjInstance *receiver, Obj *callable, int flags);
ObjInternal *newInternalObject(bool isRealObject, void *data, size_t dataSz, GCMarkFunc markFn, GCFreeFunc freeFn, int flags);
ObjClosure *newClosure(ObjFunction *function, int flags);
ObjUpvalue *newUpvalue(Value *slot, int flags);

// Object destruction functions
void freeClassInfo(ClassInfo *cinfo);

// methods/classes
Obj *instanceFindMethod(ObjInstance *obj, ObjString *name);
Obj *instanceFindMethodOrRaise(ObjInstance *obj, ObjString *name);
Obj *instanceFindGetter(ObjInstance *obj, ObjString *name);
Obj *instanceFindSetter(ObjInstance *obj, ObjString *name);
Obj *classFindStaticMethod(ObjClass *obj, ObjString *name);
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
bool isInstanceLikeObjNoClass(Obj *obj);
size_t sizeofObjType(ObjType type);
const char *objTypeName(ObjType type);
char *className(ObjClass *klass);

static inline bool isCallable(Value val) {
    return IS_CLASS(val) || IS_NATIVE_FUNCTION(val) ||
        IS_BOUND_METHOD(val) || IS_CLOSURE(val);
}

bool is_value_class_p(Value val);
bool is_value_module_p(Value val);
bool is_value_instance_p(Value val);
bool is_value_closure_p(Value val);
bool is_value_instance_of_p(Value val, ObjClass *klass);
bool is_value_a_p(Value val, ObjClass *klass);

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

#endif
