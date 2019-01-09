#include <string.h>
#include <stdio.h>
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"
#include "debug.h"
#include "runtime.h"
#include "vec.h"

// allocate object and link it to the VM object heap
#define ALLOCATE_OBJ(type, objectType, flags) \
    (type*)allocateObject(sizeof(type), objectType, flags)

extern VM vm;

#define GC_GEN_FROM_NEWOBJ_FLAGS(flags) (flags & NEWOBJ_FLAG_OLD ? GC_GEN_MAX : GC_GEN_MIN)
static Obj *allocateObject(size_t size, ObjType type, int flags) {
    DBG_ASSERT(type > OBJ_T_NONE);
    Obj *object = getNewObject(type, size, flags);
    object->type = type;

    if (vm.inited && vm.curThread && vm.curThread->inCCall > 0) {
        vec_push(&vm.curThread->stackObjects, object);
    }

    object->objectId = (size_t)object;
    object->flags = OBJ_FLAG_NONE;
    object->GCGen = GC_GEN_FROM_NEWOBJ_FLAGS(flags);
    GCStats.generations[object->GCGen]++;

    return object;
}

/**
 * Allocate a new lox string object with given characters and length
 * NOTE: length here is strlen(chars).
 */
static ObjString *allocateString(char *chars, int length, ObjClass *klass, int flags) {
    ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_T_STRING, flags);
    // NOTE: lxStringClass might be null if VM not yet initialized. This is okay, as
    // these strings are only interned strings.
    string->klass = klass;
    string->singletonKlass = NULL;
    string->finalizerFunc = NULL;
    void *tablesMem = (void*)ALLOCATE(Table, 1);
    string->fields = (Table*)tablesMem;
    initTable(string->fields);
    string->length = length;
    string->capacity = length;
    string->chars = chars;
    string->hash = 0; // lazily computed
    return string;
}

void objFreeze(Obj *obj) {
    ASSERT(obj);
    OBJ_SET_FROZEN(obj);
}

void objUnfreeze(Obj *obj) {
    ASSERT(obj);
    if (obj->type == OBJ_T_STRING) {
        ObjString *buf = (ObjString*)obj;
        if (UNLIKELY(STRING_IS_STATIC(buf))) {
            throwErrorFmt(lxErrClass, "Tried to unfreeze static String");
        }
    }
    OBJ_UNSET_FROZEN(obj);
}

uint32_t hashString(char *key, int length) {
    // FNV-1a hash. See: http://www.isthe.com/chongo/tech/comp/fnv/
    uint32_t hash = 2166136261u;

    // This is O(n) on the length of the string, but we only call this lazily.
    for (int i = 0; i < length; i++) {
        hash ^= key[i];
        hash *= 16777619;
    }

    return hash;
}

// use `*chars` as the underlying storage for the new string object
// NOTE: length here is strlen(chars)
// XXX: Do not pass a static string here, it'll break when GC tries to free it.
ObjString *takeString(char *chars, int length, int flags) {
    DBG_ASSERT(strlen(chars) == length);
    return allocateString(chars, length, lxStringClass, flags);
}

// use copy of `*chars` as the underlying storage for the new string object
// NOTE: length here is strlen(chars)
ObjString *copyString(char *chars, int length, int flags) {
    DBG_ASSERT(strlen(chars) >= length);

    char *heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    return allocateString(heapChars, length, lxStringClass, flags);
}

ObjString *hiddenString(char *chars, int len, int flags) {
    DBG_ASSERT(strlen(chars) >= len);
    ObjString *string = copyString(chars, len, flags|NEWOBJ_FLAG_HIDDEN);
    hideFromGC((Obj*)string);
    return string;
}

ObjString *internedString(char *chars, int length, int flags) {
    DBG_ASSERT(strlen(chars) >= length);
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
    if (!interned) {
        interned = hiddenString(chars, length, flags|NEWOBJ_FLAG_OLD|NEWOBJ_FLAG_FROZEN);
        ASSERT(tableSet(&vm.strings, OBJ_VAL(interned), NIL_VAL));
        STRING_SET_INTERNED(interned);
        objFreeze((Obj*)interned);
        GC_OLD((Obj*)interned);
    }
    return interned;
}

void pushString(Value self, Value pushed) {
    if (isFrozen(AS_OBJ(self))) {
        throwErrorFmt(lxErrClass, "%s", "String is frozen, cannot modify");
    }
    ObjString *lhsBuf = AS_STRING(self);
    ObjString *rhsBuf = AS_STRING(pushed);
    pushObjString(lhsBuf, rhsBuf);
}

bool objStringEquals(ObjString *a, ObjString *b) {
    DBG_ASSERT(a && b);
    if (a->length != b->length) return false;
    if (a->hash > 0 && b->hash > 0) return a->hash == b->hash;
    return strcmp(a->chars, b->chars) == 0;
}

// Copies `chars`, adds them to end of string.
// NOTE: don't use this function on a ObjString that is already a key
// for a table, it won't retrieve the value in the table anymore unless
// it's rehashed.
void pushCString(ObjString *string, char *chars, int lenToAdd) {
    DBG_ASSERT(strlen(chars) >= lenToAdd);
    ASSERT(!isFrozen((Obj*)string));

    if (UNLIKELY(lenToAdd == 0)) return;

    size_t newLen = string->length + lenToAdd;
    if (newLen > string->capacity) {
        size_t newCapa = GROW_CAPACITY(string->capacity);
        size_t newSz = newLen > newCapa ? newLen : newCapa;
        string->chars = GROW_ARRAY(string->chars, char, string->capacity+1, newSz+1);
        string->capacity = newSz;
    }
    int i = 0;
    for (i = 0; i < lenToAdd; i++) {
        char *c = chars+i;
        if (c == NULL) break;
        string->chars[string->length + i] = *c;
    }
    string->chars[string->length + i] = '\0';
    string->length += lenToAdd;
    string->hash = 0;
}

void insertCString(ObjString *string, char *chars, int lenToAdd, int at) {
    DBG_ASSERT(strlen(chars) >= lenToAdd);
    ASSERT(!isFrozen((Obj*)string));

    ASSERT(at <= string->length); // TODO: allow `at` that's larger than length, and add space in between

    if (at == string->length) {
        return pushCString(string, chars, lenToAdd);
    }

    if (lenToAdd == 0) return;

    size_t newLen = string->length + lenToAdd;
    if (newLen > string->capacity) {
        size_t newCapa = GROW_CAPACITY(string->capacity);
        size_t newSz = newLen > newCapa ? newLen : newCapa;
        string->chars = GROW_ARRAY(string->chars, char, string->capacity+1, newSz+1);
        string->capacity = newSz;
    }
    char *dest = string->chars + at + lenToAdd;
    char *src = string->chars+at;
    int charsToMove = string->length-at;
    memmove(dest, src, sizeof(char)*charsToMove);
    for (int i = 0; i < lenToAdd; i++) {
        string->chars[at+i] = chars[i];
    }
    string->length += lenToAdd;
    string->chars[string->length] = '\0';
    string->hash = 0;
}

void pushCStringFmt(ObjString *string, const char *format, ...) {
    va_list args;
    va_start(args, format);
    pushCStringVFmt(string, format, args);
    va_end(args);
}

void pushCStringVFmt(ObjString *string, const char *format, va_list ap) {
    ASSERT(!isFrozen((Obj*)string));

    char sbuf[201] = {'\0'};
    vsnprintf(sbuf, 200, format, ap);

    size_t buflen = strlen(sbuf);
    sbuf[buflen] = '\0';

    if (buflen == 0) return;

    size_t newLen = string->length + buflen;
    if (newLen > string->capacity) {
        size_t newCapa = GROW_CAPACITY(string->capacity);
        size_t newSz = newLen > newCapa ? newLen : newCapa;
        string->chars = GROW_ARRAY(string->chars, char, string->capacity+1, newSz+1);
        string->capacity = newSz;
    }

    int i = 0;
    for (i = 0; i < buflen; i++) {
        char *c = sbuf+i;
        string->chars[string->length + i] = *c;
    }
    string->chars[string->length + i] = '\0';
    string->length += buflen;
    string->hash = 0;
}

static inline void clearObjString(ObjString *string) {
    ASSERT(!isFrozen((Obj*)string));
    string->chars = GROW_ARRAY(string->chars, char, string->capacity+1, 1);
    string->chars[0] = '\0';
    string->length = 0;
    string->capacity = 0;
    string->hash = 0;
}

ObjFunction *newFunction(Chunk *chunk, Node *funcNode, int flags) {
    ObjFunction *function = ALLOCATE_OBJ(
        ObjFunction, OBJ_T_FUNCTION, flags|NEWOBJ_FLAG_OLD
    );

    function->arity = 0;
    function->numDefaultArgs = 0;
    function->numKwargs = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    function->klass = NULL;
    function->funcNode = funcNode;
    function->isSingletonMethod = false;
    function->hasRestArg = false;
    function->hasBlockArg = false;
    function->upvaluesInfo = NULL;
    if (chunk == NULL) {
        chunk = ALLOCATE(Chunk, 1);
        initChunk(chunk);
    }
    function->chunk = chunk;
    GC_PROMOTE(function, GC_GEN_YOUNG_MAX);
    return function;
}

ObjClosure *newClosure(ObjFunction *func, int flags) {
    ASSERT(func);
    // Allocate the upvalue array first so it doesn't cause the closure to get
    // collected.
    ObjUpvalue **upvalues = NULL;
    if (func->upvalueCount > 0) {
        upvalues = ALLOCATE(ObjUpvalue*, func->upvalueCount);
        for (int i = 0; i < func->upvalueCount; i++) {
            upvalues[i] = NULL;
        }
    }

    ObjClosure *closure = ALLOCATE_OBJ(
        ObjClosure, OBJ_T_CLOSURE, flags
    );
    closure->function = func;
    OBJ_WRITE(OBJ_VAL(closure), OBJ_VAL(func));
    closure->upvalues = upvalues;
    closure->upvalueCount = func->upvalueCount;
    closure->isBlock = false;
    GC_PROMOTE(closure, GC_GEN_YOUNG_MAX);
    return closure;
}

ObjUpvalue *newUpvalue(Value *slot, int flags) {
    ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_T_UPVALUE, flags);
    upvalue->closed = NIL_VAL;
    upvalue->value = slot; // stack slot
    upvalue->next = NULL; // it's the caller's responsibility to link it
    return upvalue;
}

static ClassInfo* newClassInfo(ObjString *name) {
    ClassInfo *cinfo = ALLOCATE(ClassInfo, 1);
    void *tablesMem = (void*)ALLOCATE(Table, 3);
    cinfo->methods = tablesMem;
    cinfo->getters = tablesMem + sizeof(Table);
    cinfo->setters = tablesMem + sizeof(Table)*2;
    initTable(cinfo->methods);
    initTable(cinfo->getters);
    initTable(cinfo->setters);
    cinfo->superclass = NULL;
    vec_init(&cinfo->v_includedMods);
    cinfo->singletonOf = NULL;
    cinfo->name = name;
    return cinfo;
}

// frees internal ClassInfo structures, not ClassInfo itself
void freeClassInfo(ClassInfo *classInfo) {
    freeTable(classInfo->methods);
    freeTable(classInfo->getters);
    freeTable(classInfo->setters);
    FREE_ARRAY(Table, classInfo->methods, 3);
    vec_deinit(&classInfo->v_includedMods);
}

ObjClass *newClass(ObjString *name, ObjClass *superclass, int flags) {
    ObjClass *klass = ALLOCATE_OBJ(
        ObjClass, OBJ_T_CLASS, flags|NEWOBJ_FLAG_OLD
    );
    klass->klass = lxClassClass; // this is NULL when creating object hierarchy in initVM
    klass->singletonKlass = NULL;
    klass->finalizerFunc = NULL;
    klass->classInfo = newClassInfo(name);
    if (name) {
        OBJ_WRITE(OBJ_VAL(klass), OBJ_VAL(name));
    }
    void *tablesMem = (void*)ALLOCATE(Table, 1);
    klass->fields = (Table*)tablesMem;
    initTable(klass->fields);
    klass->classInfo->superclass = superclass;
    if (superclass) {
        OBJ_WRITE(OBJ_VAL(klass), OBJ_VAL(superclass));
    }
    // during initial class hierarchy setup this is NULL
    if (nativeClassInit && isClassHierarchyCreated) {
        callVMMethod((ObjInstance*)klass, OBJ_VAL(nativeClassInit), 0, NULL, NULL);
        pop();
    }
    GC_OLD(klass);
    return klass;
}

ObjModule *newModule(ObjString *name, int flags) {
    ObjModule *mod = ALLOCATE_OBJ(
        ObjModule, OBJ_T_MODULE, flags|NEWOBJ_FLAG_OLD
    );
    ASSERT(lxModuleClass);
    mod->klass = lxModuleClass;
    mod->singletonKlass = NULL;
    mod->finalizerFunc = NULL;
    void *tablesMem = ALLOCATE(Table, 1);
    mod->fields = (Table*)tablesMem;
    initTable(mod->fields);
    mod->classInfo = newClassInfo(name);
    if (name) {
        OBJ_WRITE(OBJ_VAL(mod), OBJ_VAL(name));
    }
    // during initial class hierarchy setup this is NULL
    if (nativeModuleInit && isClassHierarchyCreated) {
        callVMMethod((ObjInstance*)mod, OBJ_VAL(nativeModuleInit), 0, NULL, NULL);
        pop();
    }
    GC_OLD(mod);
    return mod;
}

ObjArray *allocateArray(ObjClass *klass, int flags) {
    ObjArray *ary = ALLOCATE_OBJ(
        ObjArray, OBJ_T_ARRAY, flags
    );
    ary->klass = klass;
    ary->singletonKlass = NULL;
    ary->finalizerFunc = NULL;
    void *tablesMem = ALLOCATE(Table, 1);
    ary->fields = (Table*)tablesMem;
    initTable(ary->fields);
    initValueArray(&ary->valAry);
    ary->valAry.count = 0;
    return ary;
}

ObjMap *allocateMap(ObjClass *klass, int flags) {
    ObjMap *map = ALLOCATE_OBJ(
        ObjMap, OBJ_T_MAP, flags
    );
    map->klass = klass;
    map->singletonKlass = NULL;
    map->finalizerFunc = NULL;
    map->fields = ALLOCATE(Table, 1);
    // NOTE: even though fields are consecutive in struct, do NOT allocate 2 tables in one allocation
    // We might free `table` later.
    map->table = ALLOCATE(Table, 1);
    initTable(map->fields);
    initTable(map->table);
    return map;
}

// allocates a new instance object, doesn't call its constructor
ObjInstance *newInstance(ObjClass *klass, int flags) {
    // NOTE: since this is called from vm.c's doCallCallable to initialize new
    // instances when given constructor functions, this must return new
    // modules/classes when given Module() or Class() constructors
    if (LIKELY(vm.inited)) {
        DBG_ASSERT(klass);
        if (IS_SUBCLASS(klass, lxAryClass)) {
            return (ObjInstance*)allocateArray(klass, flags);
        } else if (IS_SUBCLASS(klass, lxStringClass)) {
            return (ObjInstance*)allocateString(NULL, 0, klass, flags);
        } else if (IS_SUBCLASS(klass, lxMapClass)) {
            return (ObjInstance*)allocateMap(klass, flags);
        } else if (klass == lxClassClass) {
            return (ObjInstance*)newClass(NULL, lxObjClass, flags);
        } else if (klass == lxModuleClass) {
            return (ObjInstance*)newModule(NULL, flags);
        }
    }
    ObjInstance *obj = ALLOCATE_OBJ(
        ObjInstance, OBJ_T_INSTANCE, flags
    );
    obj->klass = klass;
    obj->singletonKlass = NULL;
    obj->finalizerFunc = NULL;
    void *tablesMem = ALLOCATE(Table, 1);
    obj->fields = (Table*)tablesMem;
    initTable(obj->fields);
    obj->internal = NULL;
    return obj;
}

ObjNative *newNative(ObjString *name, NativeFn function, int flags) {
    ASSERT(function);
    ObjNative *native = ALLOCATE_OBJ(
        ObjNative, OBJ_T_NATIVE_FUNCTION, flags|NEWOBJ_FLAG_OLD
    );
    native->function = function;
    native->name = name; // should be interned
    if (name) {
        OBJ_WRITE(OBJ_VAL(native), OBJ_VAL(name));
    }
    native->klass = NULL;
    native->isStatic = false;
    GC_OLD(native);
    return native;
}

ObjBoundMethod *newBoundMethod(ObjInstance *receiver, Obj *callable, int flags) {
    ASSERT(receiver);
    ASSERT(callable);
    ObjBoundMethod *bmethod = ALLOCATE_OBJ(
        ObjBoundMethod, OBJ_T_BOUND_METHOD, flags
    );
    bmethod->receiver = OBJ_VAL(receiver);
    bmethod->callable = callable;
    OBJ_WRITE(OBJ_VAL(bmethod), OBJ_VAL(receiver));
    OBJ_WRITE(OBJ_VAL(bmethod), OBJ_VAL(callable));
    return bmethod;
}

ObjInternal *newInternalObject(bool isRealObject, void *data, size_t dataSz, GCMarkFunc markFunc, GCFreeFunc freeFunc, int flags) {
    ObjInternal *obj;
    if (isRealObject) {
        obj = ALLOCATE_OBJ(
            ObjInternal, OBJ_T_INTERNAL, flags|NEWOBJ_FLAG_OLD
        );
    } else {
        obj = ALLOCATE(ObjInternal, 1);
        memset(obj, 0, sizeof(ObjInternal));
        obj->object.type = OBJ_T_INTERNAL;
        obj->object.GCGen = 0;
        obj->object.flags = OBJ_FLAG_NONE;
    }
    obj->data = data;
    obj->dataSz = dataSz;
    obj->markFunc = markFunc;
    obj->freeFunc = freeFunc;
    obj->isRealObject = isRealObject;
    return obj;
}

Obj *instanceFindMethod(ObjInstance *obj, ObjString *name) {
    ObjClass *klass = obj->klass;
    // interned strings that are created before lxStringClass exists have no class
    if (!klass && ((Obj*) obj)->type == OBJ_T_STRING) {
        klass = lxStringClass;
        obj->klass = klass;
    }
    if (obj->singletonKlass) {
        klass = obj->singletonKlass;
    }
    Value method;
    Value nameVal = OBJ_VAL(name);
    while (klass) {
        ObjModule *mod = NULL; int i = 0;
        vec_foreach_rev(&CLASSINFO(klass)->v_includedMods, mod, i) {
            if (tableGet(CLASSINFO(mod)->methods, nameVal, &method)) {
                return AS_OBJ(method);
            }
        }
        if (tableGet(CLASSINFO(klass)->methods, nameVal, &method)) {
            return AS_OBJ(method);
        }
        klass = CLASSINFO(klass)->superclass;
    }
    return NULL;
}

Obj *instanceFindGetter(ObjInstance *obj, ObjString *name) {
    ObjClass *klass = obj->klass;
    if (obj->singletonKlass) {
        klass = obj->singletonKlass;
    }
    Value getter;
    Value nameVal = OBJ_VAL(name);
    while (klass) {
        ObjModule *mod = NULL; int i = 0;
        vec_foreach_rev(&CLASSINFO(klass)->v_includedMods, mod, i) {
            if (tableGet(CLASSINFO(mod)->getters, nameVal, &getter)) {
                return AS_OBJ(getter);
            }
        }
        if (tableGet(CLASSINFO(klass)->getters, nameVal, &getter)) {
            return AS_OBJ(getter);
        }
        klass = CLASSINFO(klass)->superclass;
    }
    return NULL;
}

Obj *instanceFindSetter(ObjInstance *obj, ObjString *name) {
    ObjClass *klass = obj->klass;
    if (obj->singletonKlass) {
        klass = obj->singletonKlass;
    }
    Value setter;
    Value nameVal = OBJ_VAL(name);
    while (klass) {
        ObjModule *mod = NULL; int i = 0;
        vec_foreach_rev(&CLASSINFO(klass)->v_includedMods, mod, i) {
            if (tableGet(CLASSINFO(mod)->setters, nameVal, &setter)) {
                return AS_OBJ(setter);
            }
        }
        if (tableGet(CLASSINFO(klass)->setters, nameVal, &setter)) {
            return AS_OBJ(setter);
        }
        klass = CLASSINFO(klass)->superclass;
    }
    return NULL;
}

Obj *instanceFindMethodOrRaise(ObjInstance *obj, ObjString *name) {
    Obj *method = instanceFindMethod(obj, name);
    if (UNLIKELY(!method)) {
        throwErrorFmt(lxNameErrClass,
            "Undefined instance method '%s' for class %s",
            name->chars, instanceClassName(obj)
        );
    }
    return method;
}

Obj *classFindStaticMethod(ObjClass *obj, ObjString *name) {
    ObjClass *klass = classSingletonClass(obj);
    Value method;
    // look up in singleton class hierarchy
    while (klass) {
        if (tableGet(CLASSINFO(klass)->methods, OBJ_VAL(name), &method)) {
            return AS_OBJ(method);
        }
        klass = CLASSINFO(klass)->superclass;
    }
    // not found, look up in class `Class` instance methods, to Object
    klass = lxClassClass;
    while (klass) {
        if (tableGet(CLASSINFO(klass)->methods, OBJ_VAL(name), &method)) {
            return AS_OBJ(method);
        }
        klass = CLASSINFO(klass)->superclass;
    }
    return NULL;
}

Obj *moduleFindStaticMethod(ObjModule *mod, ObjString *name) {
    ObjClass *klass = moduleSingletonClass(mod);
    Value method;
    // look up in singleton class hierarchy
    while (klass) {
        if (tableGet(CLASSINFO(klass)->methods, OBJ_VAL(name), &method)) {
            return AS_OBJ(method);
        }
        klass = CLASSINFO(klass)->superclass;
    }
    // not found, look up in class `Module` instance methods, to Object
    klass = lxModuleClass;
    while (klass) {
        if (tableGet(CLASSINFO(klass)->methods, OBJ_VAL(name), &method)) {
            return AS_OBJ(method);
        }
        klass = CLASSINFO(klass)->superclass;
    }
    return NULL;
}

void setObjectFinalizer(ObjInstance *obj, Obj *callable) {
    ASSERT(isCallable(OBJ_VAL(callable)));
    if (obj->finalizerFunc == NULL) {
        activeFinalizers++;
        GC_PROMOTE_ONCE(obj);
        OBJ_SET_HAS_FINALIZER(obj);
    }
    OBJ_WRITE(OBJ_VAL(obj), OBJ_VAL(callable));
    obj->finalizerFunc = callable;
}

const char *typeOfObj(Obj *obj) {
    DBG_ASSERT(obj);
    switch (obj->type) {
    case OBJ_T_STRING:
        return "string";
    case OBJ_T_ARRAY:
        return "array";
    case OBJ_T_MAP:
        return "map";
    case OBJ_T_INSTANCE:
        return "instance";
    case OBJ_T_CLASS:
        return "class";
    case OBJ_T_MODULE:
        return "module";
    case OBJ_T_CLOSURE:
        return "closure";
    case OBJ_T_INTERNAL:
        return "internal";
    case OBJ_T_FUNCTION:
    case OBJ_T_NATIVE_FUNCTION:
    case OBJ_T_BOUND_METHOD:
        return "function";
    case OBJ_T_UPVALUE:
        return "upvalue";
    default: {
        UNREACHABLE("Unknown object type: (%d)\n", obj->type);
    }
    }
}

Value newArray(void) {
    DBG_ASSERT(nativeArrayInit);
    ObjArray *ary = allocateArray(lxAryClass, NEWOBJ_FLAG_NONE);
    callVMMethod((ObjInstance*)ary, OBJ_VAL(nativeArrayInit), 0, NULL, NULL);
    DBG_ASSERT(IS_AN_ARRAY(peek(0)));
    return pop();
}

// NOTE: used in compiler, can't use VM stack
Value newArrayConstant(void) {
    ObjArray *ary = allocateArray(lxAryClass, NEWOBJ_FLAG_OLD);
    ValueArray *valAry = &ary->valAry;
    initValueArray(valAry);
    ARRAY_SET_STATIC(ary);
    GC_OLD(ary);
    return OBJ_VAL(ary);
}

// NOTE: doesn't call 'dup' function, just duplicates entries
Value arrayDup(Value otherVal) {
    ObjArray *other = AS_ARRAY(otherVal);
    Value ret = newArray();
    ObjArray *retAry = AS_ARRAY(ret);
    if (ARRAY_IS_STATIC(other)) {
        memcpy(&retAry->valAry, &other->valAry, sizeof(other->valAry));
        ARRAY_SET_SHARED(retAry);
    } else {
        for (int i = 0; i < other->valAry.count; i++) {
            writeValueArrayEnd(&retAry->valAry, other->valAry.values[i]);
        }
    }
    DBG_ASSERT(retAry->valAry.count == other->valAry.count);
    return ret;
}

void clearString(Value string) {
    if (isFrozen(AS_OBJ(string))) {
        throwErrorFmt(lxErrClass, "%s", "String is frozen, cannot modify");
    }
    ObjString *buf = AS_STRING(string);
    clearObjString(buf);
}

void stringInsertAt(Value self, Value insert, int at) {
    if (isFrozen(AS_OBJ(self))) {
        throwErrorFmt(lxErrClass, "%s", "String is frozen, cannot modify");
    }
    ObjString *selfBuf = AS_STRING(self);
    ObjString *insertBuf = AS_STRING(insert);
    insertObjString(selfBuf, insertBuf, at);
}

Value stringSubstr(Value self, int startIdx, int len) {
    if (startIdx < 0) {
        throwArgErrorFmt("%s", "start index must be positive, is: %d", startIdx);
    }
    ObjString *buf = AS_STRING(self);
    ObjString *substr = NULL;
    if (startIdx >= buf->length) {
        substr = copyString("", 0, NEWOBJ_FLAG_NONE);
    } else {
        int maxlen = buf->length-startIdx; // "abc"
        // TODO: support negative len, like `-2` also,
        // to mean up to 2nd to last char
        if (len > maxlen || len < 0) {
            len = maxlen;
        }
        substr = copyString(buf->chars+startIdx, len, NEWOBJ_FLAG_NONE);
    }

    return OBJ_VAL(substr);
}

Value stringIndexGet(Value self, int index) {
    ObjString *buf = AS_STRING(self);
    if (index >= buf->length) {
        return OBJ_VAL(copyString("", 0, NEWOBJ_FLAG_NONE));
    } else if (index < 0) { // TODO: make it works from end of str?
        throwArgErrorFmt("%s", "index cannot be negative");
    } else {
        return OBJ_VAL(copyString(buf->chars+index, 1, NEWOBJ_FLAG_NONE));
    }
}

Value stringIndexSet(Value self, int index, char c) {
    ObjString *buf = AS_STRING(self);
    if (isFrozen(AS_OBJ(self))) {
        throwErrorFmt(lxErrClass, "%s", "String is frozen, cannot modify");
    }
    if (index >= buf->length) {
        throwArgErrorFmt("%s", "index too big");
    } else if (index < 0) { // TODO: make it work from end of str?
        throwArgErrorFmt("%s", "index cannot be negative");
    } else {
        char oldC = buf->chars[index];
        buf->chars[index] = c;
        if (oldC != c) {
            buf->hash = 0;
        }
    }
    return self;
}

bool stringEquals(Value a, Value b) {
    if (!IS_STRING(b)) return false;
    return objStringEquals(AS_STRING(a), AS_STRING(b));
}

void arrayPush(Value self, Value el) {
    ObjArray *selfObj = AS_ARRAY(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    arrayDedup(selfObj);
    ValueArray *ary = &selfObj->valAry;
    writeValueArrayEnd(ary, el);
    OBJ_WRITE(OBJ_VAL(selfObj), el);
}

// Deletes the given element from the array, returning its old index if
// it was found and deleted, otherwise returns -1. Uses `valEqual()` for
// equality check.
int arrayDelete(Value self, Value el) {
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    arrayDedup(AS_ARRAY(self));
    ValueArray *ary = &AS_ARRAY(self)->valAry;
    Value val; int idx = 0; int found = -1;
    VALARRAY_FOREACH(ary, val, idx) {
        if (valEqual(el, val)) {
            found = idx;
            break;
        }
    }
    if (found != -1) {
        removeValueArray(ary, found);
    }
    return found;
}

Value arrayPop(Value self) {
    ObjArray *selfObj = AS_ARRAY(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    arrayDedup(selfObj);
    ValueArray *ary = &selfObj->valAry;
    if (ary->count == 0) return NIL_VAL;
    Value found = arrayGet(self, ary->count-1);
    removeValueArray(ary, ary->count-1);
    return found;
}

Value arrayPopFront(Value self) {
    ObjArray *selfObj = AS_ARRAY(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    arrayDedup(selfObj);
    ValueArray *ary = &selfObj->valAry;
    if (ary->count == 0) return NIL_VAL;
    Value found = arrayGet(self, 0);
    removeValueArray(ary, 0);
    return found;
}

void arrayPushFront(Value self, Value el) {
    ObjArray *selfObj = AS_ARRAY(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    arrayDedup(selfObj);
    ValueArray *ary = &selfObj->valAry;
    writeValueArrayBeg(ary, el);
    OBJ_WRITE(self, el);
}

// NOTE: doesn't check frozenness or type of `self`
void arrayClear(Value self) {
    ObjArray *selfObj = AS_ARRAY(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    arrayDedup(selfObj);
    freeValueArray(&selfObj->valAry);
}

bool arrayEquals(Value self, Value other) {
    if (!IS_AN_ARRAY(other)) return false;
    ValueArray *buf1 = &AS_ARRAY(self)->valAry;
    ValueArray *buf2 = &AS_ARRAY(other)->valAry;
    if (buf1->count != buf2->count) return false;
    for (int i = 0; i < buf1->count; i++) {
        if (!valEqual(buf1->values[i], buf2->values[i])) {
            return false;
        }
    }
    return true;
}

Value newMap(void) {
    DBG_ASSERT(nativeMapInit);
    ObjInstance *instance = newInstance(lxMapClass, NEWOBJ_FLAG_NONE);
    callVMMethod(instance, OBJ_VAL(nativeMapInit), 0, NULL, NULL);
    return pop();
}

Value mapDup(Value other) {
    Value ret = newMap();
    Table *otherMap = AS_MAP(other)->table;
    Entry e; int eidx = 0;
    TABLE_FOREACH(otherMap, e, eidx, {
        mapSet(ret, e.key, e.value);
        OBJ_WRITE(ret, e.key);
        OBJ_WRITE(ret, e.value);
    })
    return ret;
}

// NOTE: used in compiler, can't use VM stack
Value newMapConstant(void) {
    ObjMap *map = allocateMap(lxMapClass, NEWOBJ_FLAG_OLD);
    return OBJ_VAL(map);
}

bool mapEquals(Value self, Value other) {
    if (!IS_A_MAP(other)) return false;
    Table *map1 = AS_MAP(self)->table;
    Table *map2 = AS_MAP(other)->table;
    if (map1->count != map2->count) return false;
    Entry e; int idx = 0;
    Value val2;
    TABLE_FOREACH(map1, e, idx, {
        if (!tableGet(map2, e.key, &val2)) {
            return false;
        }
        if (!valEqual(e.value, val2)) {
            return false;
        }
    })
    return true;
}

Value getProp(Value self, ObjString *propName) {
    DBG_ASSERT(IS_INSTANCE_LIKE(self));
    ObjInstance *inst = AS_INSTANCE(self);
    Value ret;
    if (tableGet(inst->fields, OBJ_VAL(propName), &ret)) {
        return ret;
    } else {
        return NIL_VAL;
    }
}

// NOTE: doesn't check frozenness of `self`
void setProp(Value self, ObjString *propName, Value val) {
    DBG_ASSERT(IS_INSTANCE_LIKE(self));
    ObjInstance *inst = AS_INSTANCE(self);
    tableSet(inst->fields, OBJ_VAL(propName), val);
    OBJ_WRITE(self, val);
}

bool instanceIsA(ObjInstance *inst, ObjClass *klass) {
    ObjClass *instKlass = inst->klass;
    if (instKlass == klass) return true;
    while (instKlass != NULL && instKlass != klass) {
        instKlass = CLASSINFO(instKlass)->superclass;
    }
    return instKlass == klass;
}

Value newError(ObjClass *errClass, Value msg) {
    DBG_ASSERT(IS_SUBCLASS(errClass, lxErrClass));
    push(OBJ_VAL(errClass));
    push(msg); // argument
    callCallable(OBJ_VAL(errClass), 1, false, NULL);
    Value err = pop();
    DBG_ASSERT(IS_AN_ERROR(err));
    return err;
}

bool isSubclass(ObjClass *subklass, ObjClass *superklass) {
    DBG_ASSERT(subklass);
    DBG_ASSERT(superklass);
    if (subklass == superklass) { return true; }
    while (subklass != NULL && subklass != superklass) {
        ClassInfo *cinfo = CLASSINFO(subklass);
        DBG_ASSERT(cinfo);
        subklass = cinfo->superclass;
    }
    return subklass != NULL;
}

static const char *anonClassName = "(anon)";

const char *instanceClassName(ObjInstance *obj) {
    DBG_ASSERT(obj);
    ObjClass *klass = obj->klass;
    if (!klass || !CLASSINFO(klass)->name) return anonClassName;
    return CLASSINFO(klass)->name->chars;
}

ObjClass *singletonClass(Obj *obj) {
    switch (obj->type) {
        case OBJ_T_INSTANCE:
            return instanceSingletonClass((ObjInstance*)obj);
        case OBJ_T_CLASS:
            return classSingletonClass((ObjClass*)obj);
        case OBJ_T_MODULE:
            return moduleSingletonClass((ObjModule*)obj);
        default:
            UNREACHABLE_RETURN(NULL);
    }
}

ObjClass *instanceSingletonClass(ObjInstance *inst) {
    if (inst->singletonKlass) {
        return inst->singletonKlass;
    }
    ObjString *name = valueToString(OBJ_VAL(inst), hiddenString, NEWOBJ_FLAG_OLD);
    pushCString(name, " (meta)", 7);
    ObjClass *meta = newClass(name, inst->klass, NEWOBJ_FLAG_OLD);
    CLASSINFO(meta)->singletonOf = (Obj*)inst;
    inst->singletonKlass = meta;
    OBJ_WRITE(OBJ_VAL(inst), OBJ_VAL(meta));
    unhideFromGC((Obj*)name);
    return meta;
}

ObjClass *classSingletonClass(ObjClass *klass) {
    if (klass->singletonKlass) {
        return klass->singletonKlass;
    }
    ObjString *name = NULL;
    if (CLASSINFO(klass)->name) {
        name = dupString(CLASSINFO(klass)->name); // TODO: create as old
    } else {
        name = copyString("(anon)", 6, NEWOBJ_FLAG_OLD);
        CLASSINFO(klass)->name = name;
    }
    pushCString(name, " (meta)", 7);
    ObjClass *meta = newClass(name, CLASSINFO(klass)->superclass, NEWOBJ_FLAG_OLD);
    CLASSINFO(meta)->singletonOf = (Obj*)klass;
    klass->singletonKlass = meta;
    OBJ_WRITE(OBJ_VAL(klass), OBJ_VAL(meta));
    /*klass->superclass = meta;*/
    return meta;
}

ObjClass *moduleSingletonClass(ObjModule *mod) {
    if (mod->singletonKlass) {
        return mod->singletonKlass;
    }
    ObjString *name = NULL;
    if (CLASSINFO(mod)->name) {
        name = dupString(CLASSINFO(mod)->name); // TODO: make old
        hideFromGC((Obj*)name);
    } else {
        name = hiddenString("(anon)", 6, NEWOBJ_FLAG_OLD);
        CLASSINFO(mod)->name = name;
    }
    pushCString(name, " (meta)", 7);
    ObjClass *meta = newClass(name, lxClassClass, NEWOBJ_FLAG_OLD);
    mod->singletonKlass = meta;
    OBJ_WRITE(OBJ_VAL(mod), OBJ_VAL(meta));
    CLASSINFO(meta)->singletonOf = (Obj*)mod;
    unhideFromGC((Obj*)name);
    return meta;
}

Value newThread(void) {
    if (!vm.inited) { // creating main thread in initVM
        ASSERT(vm.mainThread == NULL);
        ObjInstance *instance = newInstance(NULL, NEWOBJ_FLAG_OLD);
        // no stack frame, just use function
        Value threadVal = OBJ_VAL(instance);
        lxThreadInit(1, &threadVal);
        GC_OLD(instance);
        return threadVal;
    } else {
        ObjInstance *instance = newInstance(lxThreadClass, NEWOBJ_FLAG_OLD);
        GC_PROMOTE(instance, GC_GEN_OLD_MIN);
        callVMMethod(instance, OBJ_VAL(nativeThreadInit), 0, NULL, NULL);
        return pop();
    }
}

Value newBlock(Obj *callable) {
    DBG_ASSERT(nativeBlockInit);
    ObjInstance *instance = newInstance(lxBlockClass, NEWOBJ_FLAG_NONE);
    Value callableVal = OBJ_VAL(callable);
    callVMMethod(instance, OBJ_VAL(nativeBlockInit), 1, &callableVal, NULL);
    return pop();
}

bool isInstanceLikeObj(Obj *obj) {
    if (!obj) return false;
    if (!IS_OBJ(OBJ_VAL(obj))) return false;
    switch (obj->type) {
        case OBJ_T_INSTANCE:
        case OBJ_T_ARRAY:
        case OBJ_T_MAP:
        case OBJ_T_STRING:
        case OBJ_T_CLASS:
        case OBJ_T_MODULE:
            return true;
        default:
            return false;
    }
}

bool isInstanceLikeObjNoClass(Obj *obj) {
    if (!obj) return false;
    if (!IS_OBJ(OBJ_VAL(obj))) return false;
    switch (obj->type) {
        case OBJ_T_INSTANCE:
        case OBJ_T_ARRAY:
        case OBJ_T_MAP:
        case OBJ_T_STRING:
            return true;
        default:
            return false;
    }
}

size_t sizeofObjType(ObjType type) {
    switch (type) {
        case OBJ_T_STRING:
            return sizeof(ObjString);
        case OBJ_T_ARRAY:
            return sizeof(ObjArray);
        case OBJ_T_MAP:
            return sizeof(ObjMap);
        case OBJ_T_FUNCTION:
            return sizeof(ObjFunction);
        case OBJ_T_INSTANCE:
            return sizeof(ObjInstance);
        case OBJ_T_CLASS:
            return sizeof(ObjClass);
        case OBJ_T_MODULE:
            return sizeof(ObjModule);
        case OBJ_T_NATIVE_FUNCTION:
            return sizeof(ObjNative);
        case OBJ_T_BOUND_METHOD:
            return sizeof(ObjBoundMethod);
        case OBJ_T_UPVALUE:
            return sizeof(ObjUpvalue);
        case OBJ_T_CLOSURE:
            return sizeof(ObjClosure);
        case OBJ_T_INTERNAL:
            return sizeof(ObjInternal);
        default:
            UNREACHABLE_RETURN(0);
    }
}

const char *objTypeName(ObjType type) {
    switch (type) {
        case OBJ_T_STRING:
            return "T_STRING";
        case OBJ_T_ARRAY:
            return "T_ARRAY";
        case OBJ_T_MAP:
            return "T_MAP";
        case OBJ_T_INSTANCE:
            return "T_INSTANCE";
        case OBJ_T_FUNCTION:
            return "T_FUNCTION";
        case OBJ_T_CLASS:
            return "T_CLASS";
        case OBJ_T_MODULE:
            return "T_MODULE";
        case OBJ_T_NATIVE_FUNCTION:
            return "T_NATIVE_FUNCTION";
        case OBJ_T_CLOSURE:
            return "T_CLOSURE";
        case OBJ_T_BOUND_METHOD:
            return "T_BOUND_METHOD";
        case OBJ_T_UPVALUE:
            return "T_UPVALUE";
        case OBJ_T_INTERNAL:
            return "T_INTERNAL";
        case OBJ_T_NONE:
            return "T_NONE";
        default:
            UNREACHABLE_RETURN("invalid type");
    }
}

char *className(ObjClass *klass) {
    if (CLASSINFO(klass)->name) {
        return CLASSINFO(klass)->name->chars;
    } else {
        return "(anon)";
    }
}

bool is_value_class_p(Value val) {
    return IS_CLASS(val);
}
bool is_value_module_p(Value val) {
    return IS_MODULE(val);
}
bool is_value_instance_p(Value val) {
    return IS_INSTANCE(val);
}
bool is_value_closure_p(Value val) {
    return IS_CLOSURE(val);
}
bool is_value_instance_of_p(Value val, ObjClass *klass) {
    return IS_INSTANCE_LIKE(val) && AS_INSTANCE(val)->klass == klass;
}
bool is_value_a_p(Value val, ObjClass *klass) {
    return IS_INSTANCE_LIKE(val) && instanceIsA(AS_INSTANCE(val), klass);
}
