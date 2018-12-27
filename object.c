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
#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

extern VM vm;
extern unsigned inCCall;

static Obj *allocateObject(size_t size, ObjType type) {
    ASSERT(vm.inited);
    Obj *object = (Obj*)reallocate(NULL, 0, size);
    ASSERT(type > OBJ_T_NONE);
    object->type = type;
    object->isDark = true; // don't collect right away, wait at least 1 round of GC
    object->isFrozen = false;

    if (inCCall > 0) {
        vec_push(&vm.stackObjects, object);
    }

    // prepend new object to linked list
    object->next = vm.objects;
    if (vm.objects) {
        vm.objects->prev = object;
    }
    object->prev = NULL;
    vm.objects = object;
    object->isLinked = true;
    object->objectId = (size_t)object;
    object->noGC = false;
    object->GCGen = 0;
    GCProf.generations[object->noGC]++;

    return object;
}

/**
 * Allocate a new lox string object with given characters and length
 * NOTE: length here is strlen(chars).
 */
static ObjString *allocateString(char *chars, int length) {
    if (!vm.inited) {
        fprintf(stderr, "allocateString before VM inited: %s\n", chars);
        ASSERT(vm.inited);
    }
    ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_T_STRING);
    string->length = length;
    string->capacity = length;
    string->chars = chars;
    string->hash = 0; // lazily computed
    string->isInterned = false;
    string->isStatic = false;
    return string;
}

void objFreeze(Obj *obj) {
    ASSERT(obj);
    obj->isFrozen = true;
}

void objUnfreeze(Obj *obj) {
    ASSERT(obj);
    if (obj->type == OBJ_T_INSTANCE) {
        ObjInstance *instance = (ObjInstance*)obj;
        if (instance->klass == lxStringClass) {
            ObjString *buf = STRING_GETHIDDEN(OBJ_VAL(obj));
            if (buf->isStatic) {
                throwErrorFmt(lxErrClass, "Tried to unfreeze static String");
            }
        }
    }
    obj->isFrozen = false;
}

bool isFrozen(Obj *obj) {
    ASSERT(obj);
    return obj->isFrozen;
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
ObjString *takeString(char *chars, int length) {
    DBG_ASSERT(strlen(chars) == length);
    return allocateString(chars, length);
}

// use copy of `*chars` as the underlying storage for the new string object
// NOTE: length here is strlen(chars)
ObjString *copyString(char *chars, int length) {
    DBG_ASSERT(strlen(chars) >= length);

    char *heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    return allocateString(heapChars, length);
}

ObjString *hiddenString(char *chars, int len) {
    DBG_ASSERT(strlen(chars) >= len);
    ObjString *string = copyString(chars, len);
    hideFromGC((Obj*)string);
    return string;
}


ObjString *internedString(char *chars, int length) {
    DBG_ASSERT(strlen(chars) >= length);
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
    if (!interned) {
        interned = copyString(chars, length);
        tableSet(&vm.strings, OBJ_VAL(interned), NIL_VAL);
        interned->isInterned = true;
        objFreeze((Obj*)interned);
        GCPromote((Obj*)interned, GC_GEN_MAX);
    }
    return interned;
}

ObjString *dupString(ObjString *string) {
    ASSERT(string);
    ObjString *dup = copyString(string->chars, string->length);
    dup->hash = string->hash;
    return dup;
}

void pushString(Value self, Value pushed) {
    if (isFrozen(AS_OBJ(self))) {
        throwErrorFmt(lxErrClass, "%s", "String is frozen, cannot modify");
    }
    ObjString *lhsBuf = STRING_GETHIDDEN(self);
    ObjString *rhsBuf = STRING_GETHIDDEN(pushed);
    pushObjString(lhsBuf, rhsBuf);
}

void pushObjString(ObjString *a, ObjString *b) {
    pushCString(a, b->chars, b->length);
}

void insertObjString(ObjString *a, ObjString *b, int at) {
    insertCString(a, b->chars, b->length, at);
}

bool objStringEquals(ObjString *a, ObjString *b) {
    DBG_ASSERT(a && b);
    if (a->length != b->length) return false;
    if (a->hash > 0 && b->hash > 0) return a->hash == b->hash;
    return strcmp(a->chars, b->chars) == 0;
}

// Copies `chars`, adds them to end of string.
// NOTE: don't use this function on a ObjString that is already a key
// for a table, it won't retrieve the value in the table anymore.
void pushCString(ObjString *string, char *chars, int lenToAdd) {
    DBG_ASSERT(strlen(chars) >= lenToAdd);
    ASSERT(!((Obj*)string)->isFrozen);

    if (lenToAdd == 0) return;

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
    ASSERT(!((Obj*)string)->isFrozen);

    ASSERT(at <= string->length); // FIXME: allow `at` that's larger than length, and add space in between

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
    string->hash = 0;
}

void pushCStringFmt(ObjString *string, const char *format, ...) {
    va_list args;
    va_start(args, format);
    pushCStringVFmt(string, format, args);
    va_end(args);
}

void pushCStringVFmt(ObjString *string, const char *format, va_list ap) {
    ASSERT(!((Obj*)string)->isFrozen);

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

void clearObjString(ObjString *string) {
    ASSERT(!((Obj*)string)->isFrozen);
    string->chars = GROW_ARRAY(string->chars, char, string->capacity+1, 1);
    string->chars[0] = '\0';
    string->length = 0;
    string->capacity = 0;
    string->hash = 0;
}

ObjFunction *newFunction(Chunk *chunk, Node *funcNode) {
    ObjFunction *function = ALLOCATE_OBJ(
        ObjFunction, OBJ_T_FUNCTION
    );

    function->arity = 0;
    function->numDefaultArgs = 0;
    function->hasRestArg = false;
    function->numKwargs = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    function->isMethod = false;
    function->funcNode = funcNode;
    if (chunk == NULL) {
        initChunk(&function->chunk);
    } else {
        function->chunk = *chunk; // copy
    }
    return function;
}

ObjClosure *newClosure(ObjFunction *func) {
    ASSERT(func);
    // Allocate the upvalue array first so it doesn't cause the closure to get
    // collected.
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, func->upvalueCount);
    for (int i = 0; i < func->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure *closure = ALLOCATE_OBJ(
        ObjClosure, OBJ_T_CLOSURE
    );
    closure->function = func;
    closure->upvalues = upvalues;
    closure->upvalueCount = func->upvalueCount;
    return closure;
}

ObjUpvalue *newUpvalue(Value *slot) {
    ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_T_UPVALUE);
    upvalue->closed = NIL_VAL;
    upvalue->value = slot;
    upvalue->next = NULL; // it's the caller's responsibility to link it
    return upvalue;
}

ObjClass *newClass(ObjString *name, ObjClass *superclass) {
    ObjClass *klass = ALLOCATE_OBJ(
        ObjClass, OBJ_T_CLASS
    );
    klass->klass = lxClassClass; // this is NULL when creating object hierarchy in initVM
    klass->singletonKlass = NULL;
    klass->finalizerFunc = NULL;
    klass->singletonOf = NULL;
    initTable(&klass->fields);
    initTable(&klass->hiddenFields);
    initTable(&klass->methods);
    initTable(&klass->getters);
    initTable(&klass->setters);
    vec_init(&klass->v_includedMods);
    klass->name = name; // can be NULL
    klass->superclass = superclass;
    // during initial class hierarchy setup this is NULL
    if (nativeClassInit && isClassHierarchyCreated) {
        callVMMethod((ObjInstance*)klass, OBJ_VAL(nativeClassInit), 0, NULL);
        pop();
    }
    return klass;
}

ObjModule *newModule(ObjString *name) {
    ObjModule *mod = ALLOCATE_OBJ(
        ObjModule, OBJ_T_MODULE
    );
    ASSERT(lxModuleClass);
    mod->klass = lxModuleClass;
    mod->singletonKlass = NULL;
    mod->finalizerFunc = NULL;
    initTable(&mod->fields);
    initTable(&mod->hiddenFields);
    initTable(&mod->methods);
    initTable(&mod->getters);
    initTable(&mod->setters);
    mod->name = name; // can be NULL
    // during initial class hierarchy setup this is NULL
    if (nativeModuleInit && isClassHierarchyCreated) {
        callVMMethod((ObjInstance*)mod, OBJ_VAL(nativeModuleInit), 0, NULL);
        pop();
    }
    return mod;
}

// allocates a new instance object, doesn't call its constructor
ObjInstance *newInstance(ObjClass *klass) {
    ASSERT(klass);
    // NOTE: since this is called from vm.c's doCallCallable to initialize new
    // instances when given constructor functions, this must return new
    // modules/classes when given Module() or Class() constructors
    if (klass == lxModuleClass) {
        return (ObjInstance*)newModule(NULL);
    } else if (klass == lxClassClass) {
        return (ObjInstance*)newClass(NULL, lxObjClass);
    }
    ObjInstance *obj = ALLOCATE_OBJ(
        ObjInstance, OBJ_T_INSTANCE
    );
    obj->klass = klass;
    obj->singletonKlass = NULL;
    obj->finalizerFunc = NULL;
    initTable(&obj->fields);
    initTable(&obj->hiddenFields);
    return obj;
}

ObjNative *newNative(ObjString *name, NativeFn function) {
    ASSERT(function);
    ObjNative *native = ALLOCATE_OBJ(
        ObjNative, OBJ_T_NATIVE_FUNCTION
    );
    native->function = function;
    native->name = name;
    native->klass = NULL;
    native->isStatic = false;
    return native;
}

ObjBoundMethod *newBoundMethod(ObjInstance *receiver, Obj *callable) {
    ASSERT(receiver);
    ASSERT(callable);
    ObjBoundMethod *bmethod = ALLOCATE_OBJ(
        ObjBoundMethod, OBJ_T_BOUND_METHOD
    );
    bmethod->receiver = OBJ_VAL(receiver);
    bmethod->callable = callable;
    return bmethod;
}

ObjInternal *newInternalObject(void *data, size_t dataSz, GCMarkFunc markFunc, GCFreeFunc freeFunc) {
    ObjInternal *obj = ALLOCATE_OBJ(
        ObjInternal, OBJ_T_INTERNAL
    );
    obj->data = data;
    obj->dataSz = dataSz;
    obj->markFunc = markFunc;
    obj->freeFunc = freeFunc;
    return obj;
}

Obj *instanceFindMethod(ObjInstance *obj, ObjString *name) {
    ObjClass *klass = obj->klass;
    if (obj->singletonKlass) {
        klass = obj->singletonKlass;
    }
    Value method;
    Value nameVal = OBJ_VAL(name);
    while (klass) {
        ObjModule *mod = NULL; int i = 0;
        vec_foreach_rev(&klass->v_includedMods, mod, i) {
            if (tableGet(&mod->methods, nameVal, &method)) {
                return AS_OBJ(method);
            }
        }
        if (tableGet(&klass->methods, nameVal, &method)) {
            return AS_OBJ(method);
        }
        klass = klass->superclass;
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
        vec_foreach_rev(&klass->v_includedMods, mod, i) {
            if (tableGet(&mod->getters, nameVal, &getter)) {
                return AS_OBJ(getter);
            }
        }
        if (tableGet(&klass->getters, nameVal, &getter)) {
            return AS_OBJ(getter);
        }
        klass = klass->superclass;
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
        vec_foreach_rev(&klass->v_includedMods, mod, i) {
            if (tableGet(&mod->setters, nameVal, &setter)) {
                return AS_OBJ(setter);
            }
        }
        if (tableGet(&klass->setters, nameVal, &setter)) {
            return AS_OBJ(setter);
        }
        klass = klass->superclass;
    }
    return NULL;
}

Obj *instanceFindMethodOrRaise(ObjInstance *obj, ObjString *name) {
    Obj *method = instanceFindMethod(obj, name);
    if (!method) {
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
        if (tableGet(&klass->methods, OBJ_VAL(name), &method)) {
            return AS_OBJ(method);
        }
        klass = klass->superclass;
    }
    // not found, look up in class `Class` instance methods, to Object
    klass = lxClassClass;
    while (klass) {
        if (tableGet(&klass->methods, OBJ_VAL(name), &method)) {
            return AS_OBJ(method);
        }
        klass = klass->superclass;
    }
    return NULL;
}

Obj *moduleFindStaticMethod(ObjModule *mod, ObjString *name) {
    ObjClass *klass = moduleSingletonClass(mod);
    Value method;
    // look up in singleton class hierarchy
    while (klass) {
        if (tableGet(&klass->methods, OBJ_VAL(name), &method)) {
            return AS_OBJ(method);
        }
        klass = klass->superclass;
    }
    // not found, look up in class `Module` instance methods, to Object
    klass = lxModuleClass;
    while (klass) {
        if (tableGet(&klass->methods, OBJ_VAL(name), &method)) {
            return AS_OBJ(method);
        }
        klass = klass->superclass;
    }
    return NULL;
}

void *internalGetData(ObjInternal *obj) {
    return obj->data;
}

void setObjectFinalizer(ObjInstance *obj, Obj *callable) {
    ASSERT(isCallable(OBJ_VAL(callable)));
    obj->finalizerFunc = callable;
}

const char *typeOfObj(Obj *obj) {
    DBG_ASSERT(obj);
    switch (obj->type) {
    case OBJ_T_STRING:
        return "string";
    case OBJ_T_CLASS:
        return "class";
    case OBJ_T_MODULE:
        return "module";
    case OBJ_T_INSTANCE:
        return "instance";
    case OBJ_T_FUNCTION:
    case OBJ_T_NATIVE_FUNCTION:
    case OBJ_T_BOUND_METHOD:
        return "function";
    case OBJ_T_INTERNAL:
        return "internal";
    case OBJ_T_CLOSURE:
        return "closure";
    case OBJ_T_UPVALUE:
        return "upvalue";
    default: {
        UNREACHABLE("Unknown object type: (%d)\n", obj->type);
    }
    }
}

/**
 * NOTE: assumes idx is appropriate and within given range. See
 * ARRAY_SIZE(value)
 */
Value arrayGet(Value aryVal, int idx) {
    ValueArray *ary = ARRAY_GETHIDDEN(aryVal);
    return ary->values[idx];
}

int arraySize(Value aryVal) {
    ValueArray *ary = ARRAY_GETHIDDEN(aryVal);
    return ary->count;
}

ValueArray *arrayGetHidden(Value aryVal) {
    ASSERT(IS_AN_ARRAY(aryVal));
    ObjInstance *inst = AS_INSTANCE(aryVal);
    Value internalObjVal;
    ASSERT(tableGet(&inst->hiddenFields, OBJ_VAL(internedString("ary", 3)), &internalObjVal));
    ValueArray *ary = (ValueArray*)internalGetData(AS_INTERNAL(internalObjVal));
    ASSERT(ary);
    return ary;
}

Value newArray(void) {
    DBG_ASSERT(nativeArrayInit);
    ObjInstance *instance = newInstance(lxAryClass);
    callVMMethod(instance, OBJ_VAL(nativeArrayInit), 0, NULL);
    return pop();
}

// duplicates a string instance
Value dupStringInstance(Value instance) {
    ObjString *buf = STRING_GETHIDDEN(instance);
    return newStringInstance(dupString(buf));
}

// creates a new string instance, using `buf` as underlying storage
Value newStringInstance(ObjString *buf) {
    ASSERT(buf);
    DBG_ASSERT(nativeStringInit);
    ObjInstance *instance = newInstance(lxStringClass);
    Value bufVal = OBJ_VAL(buf);
    callVMMethod(instance, OBJ_VAL(nativeStringInit), 1, &bufVal);
    return pop();
}

void clearString(Value string) {
    if (isFrozen(AS_OBJ(string))) {
        throwErrorFmt(lxErrClass, "%s", "String is frozen, cannot modify");
    }
    ObjString *buf = STRING_GETHIDDEN(string);
    clearObjString(buf);
}

void stringInsertAt(Value self, Value insert, int at) {
    if (isFrozen(AS_OBJ(self))) {
        throwErrorFmt(lxErrClass, "%s", "String is frozen, cannot modify");
    }
    ObjString *selfBuf = STRING_GETHIDDEN(self);
    ObjString *insertBuf = STRING_GETHIDDEN(insert);
    insertObjString(selfBuf, insertBuf, at);
}

Value stringSubstr(Value self, int startIdx, int len) {
    if (startIdx < 0) {
        throwArgErrorFmt("%s", "start index must be positive, is: %d", startIdx);
    }
    ObjString *buf = STRING_GETHIDDEN(self);
    ObjString *substr = NULL;
    if (startIdx >= buf->length) {
        substr = copyString("", 0);
    } else {
        int maxlen = buf->length-startIdx; // "abc"
        // TODO: support negative len, like `-2` also,
        // to mean up to 2nd to last char
        if (len > maxlen || len < 0) {
            len = maxlen;
        }
        substr = copyString(buf->chars+startIdx, len);
    }

    return newStringInstance(substr);
}

Value stringIndexGet(Value self, int index) {
    ObjString *buf = STRING_GETHIDDEN(self);
    if (index >= buf->length) {
        return newStringInstance(copyString("", 0));
    } else if (index < 0) { // TODO: make it works from end of str?
        throwArgErrorFmt("%s", "index cannot be negative");
    } else {
        return newStringInstance(copyString(buf->chars+index, 1));
    }
}

Value stringIndexSet(Value self, int index, char c) {
    ObjString *buf = STRING_GETHIDDEN(self);
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
    if (!IS_A_STRING(b)) return false;
    return objStringEquals(STRING_GETHIDDEN(a), STRING_GETHIDDEN(b));
}

void arrayPush(Value self, Value el) {
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    ValueArray *ary = ARRAY_GETHIDDEN(self);
    writeValueArrayEnd(ary, el);
}

// Deletes the given element from the array, returning its old index if
// it was found and deleted, otherwise returns -1. Uses `valEqual()` for
// equality check.
int arrayDelete(Value self, Value el) {
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    ValueArray *ary = ARRAY_GETHIDDEN(self);
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
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    ValueArray *ary = ARRAY_GETHIDDEN(self);
    if (ary->count == 0) return NIL_VAL;
    Value found = arrayGet(self, ary->count-1);
    removeValueArray(ary, ary->count-1);
    return found;
}

Value arrayPopFront(Value self) {
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    ValueArray *ary = ARRAY_GETHIDDEN(self);
    if (ary->count == 0) return NIL_VAL;
    Value found = arrayGet(self, 0);
    removeValueArray(ary, 0);
    return found;
}

void arrayPushFront(Value self, Value el) {
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    ValueArray *ary = ARRAY_GETHIDDEN(self);
    writeValueArrayBeg(ary, el);
}

// NOTE: doesn't check frozenness or type of `self`
void arrayClear(Value self) {
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    freeValueArray(ARRAY_GETHIDDEN(self));
}

bool arrayEquals(Value self, Value other) {
    if (!IS_AN_ARRAY(other)) return false;
    ValueArray *buf1 = ARRAY_GETHIDDEN(self);
    ValueArray *buf2 = ARRAY_GETHIDDEN(other);
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
    ObjInstance *instance = newInstance(lxMapClass);
    callVMMethod(instance, OBJ_VAL(nativeMapInit), 0, NULL);
    return pop();
}

bool mapGet(Value mapVal, Value key, Value *ret) {
    Table *map = MAP_GETHIDDEN(mapVal);
    if (tableGet(map, key, ret)) {
        return true;
    } else {
        return false;
    }
}

// NOTE: doesn't check frozenness or type of `mapVal`
void mapSet(Value mapVal, Value key, Value val) {
    Table *map = MAP_GETHIDDEN(mapVal);
    tableSet(map, key, val);
}

// number of key-value pairs
Value mapSize(Value mapVal) {
    Table *map = MAP_GETHIDDEN(mapVal);
    return NUMBER_VAL(map->count);
}

// NOTE: doesn't check frozenness or type of `mapVal`
void mapClear(Value mapVal) {
    Table *map = MAP_GETHIDDEN(mapVal);
    freeTable(map);
}

bool mapEquals(Value self, Value other) {
    if (!IS_A_MAP(other)) return false;
    Table *map1 = MAP_GETHIDDEN(self);
    Table *map2 = MAP_GETHIDDEN(other);
    if (map1->count != map2->count) return false;
    Entry e; int idx = 0;
    Value val2;
    TABLE_FOREACH(map1, e, idx) {
        if (!tableGet(map2, e.key, &val2)) {
            return false;
        }
        if (!valEqual(e.value, val2)) {
            return false;
        }
    }
    return true;
}

Table *mapGetHidden(Value mapVal) {
    ASSERT(IS_A_MAP(mapVal));
    ObjInstance *inst = AS_INSTANCE(mapVal);
    Value internalObjVal;
    ASSERT(tableGet(&inst->hiddenFields, OBJ_VAL(internedString("map", 3)), &internalObjVal));
    Table *map = (Table*)internalGetData(AS_INTERNAL(internalObjVal));
    ASSERT(map);
    return map;
}

ObjString *stringGetHidden(Value instance) {
    ASSERT(IS_A_STRING(instance));
    ObjInstance *inst = AS_INSTANCE(instance);
    Value stringVal;
    if (tableGet(&inst->hiddenFields, OBJ_VAL(internedString("buf", 3)), &stringVal)) {
        return (ObjString*)AS_OBJ(stringVal);
    } else {
        return NULL;
    }
}

Value getProp(Value self, ObjString *propName) {
    ASSERT(IS_INSTANCE_LIKE(self));
    ObjInstance *inst = AS_INSTANCE(self);
    Value ret;
    if (tableGet(&inst->fields, OBJ_VAL(propName), &ret)) {
        return ret;
    } else {
        return NIL_VAL;
    }
}

Value getHiddenProp(Value self, ObjString *propName) {
    ASSERT(IS_INSTANCE_LIKE(self));
    ObjInstance *inst = AS_INSTANCE(self);
    Value ret;
    if (tableGet(&inst->hiddenFields, OBJ_VAL(propName), &ret)) {
        return ret;
    } else {
        return NIL_VAL;
    }
}

// NOTE: doesn't check frozenness of `self`
void setProp(Value self, ObjString *propName, Value val) {
    ASSERT(IS_INSTANCE_LIKE(self));
    ObjInstance *inst = AS_INSTANCE(self);
    tableSet(&inst->fields, OBJ_VAL(propName), val);
}

bool instanceIsA(ObjInstance *inst, ObjClass *klass) {
    ObjClass *instKlass = inst->klass;
    while (instKlass != NULL && instKlass != klass) {
        instKlass = instKlass->superclass;
    }
    return instKlass != NULL;
}

Value newError(ObjClass *errClass, Value msg) {
    ASSERT(IS_SUBCLASS(errClass, lxErrClass));
    push(OBJ_VAL(errClass));
    push(msg); // argument
    callCallable(OBJ_VAL(errClass), 1, false, NULL);
    Value err = pop();
    ASSERT(IS_AN_ERROR(err));
    return err;
}

bool isSubclass(ObjClass *subklass, ObjClass *superklass) {
    ASSERT(subklass);
    ASSERT(superklass);
    while (subklass != NULL && subklass != superklass) {
        subklass = subklass->superclass;
    }
    return subklass != NULL;
}

static const char *anonClassName = "(anon)";

const char *instanceClassName(ObjInstance *obj) {
    ASSERT(obj);
    ObjClass *klass = obj->klass;
    if (!klass || !klass->name) return anonClassName;
    return klass->name->chars;
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
    ObjString *name = valueToString(OBJ_VAL(inst), hiddenString);
    pushCString(name, " (meta)", 7);
    ObjClass *meta = newClass(name, inst->klass);
    meta->singletonOf = (Obj*)inst;
    inst->singletonKlass = meta;
    unhideFromGC((Obj*)name);
    return meta;
}

ObjClass *classSingletonClass(ObjClass *klass) {
    if (klass->singletonKlass) {
        return klass->singletonKlass;
    }
    ObjString *name = NULL;
    if (klass->name) {
        name = dupString(klass->name);
    } else {
        name = copyString("(anon)", 6);
        klass->name = name;
    }
    pushCString(name, " (meta)", 7);
    ObjClass *meta = newClass(name, klass->superclass);
    meta->singletonOf = (Obj*)klass;
    klass->singletonKlass = meta;
    /*klass->superclass = meta;*/
    return meta;
}

ObjClass *moduleSingletonClass(ObjModule *mod) {
    if (mod->singletonKlass) {
        return mod->singletonKlass;
    }
    ObjString *name = NULL;
    if (mod->name) {
        name = dupString(mod->name);
        hideFromGC((Obj*)name);
    } else {
        name = hiddenString("(anon)", 6);
        mod->name = name;
    }
    pushCString(name, " (meta)", 7);
    ObjClass *meta = newClass(name, lxClassClass);
    mod->singletonKlass = meta;
    meta->singletonOf = (Obj*)mod;
    unhideFromGC((Obj*)name);
    return meta;
}

Value newThread(void) {
    ObjInstance *instance = newInstance(lxThreadClass);
    callVMMethod(instance, OBJ_VAL(nativeThreadInit), 0, NULL);
    return pop();
}

LxThread *threadGetInternal(Value thread) {
    Value internal = getHiddenProp(thread, internedString("th", 2));
    ObjInternal *i = AS_INTERNAL(internal);
    ASSERT(i->data);
    return (LxThread*)i->data;
}

void threadSetStatus(Value thread, ThreadStatus status) {
    LxThread *th = threadGetInternal(thread);
    th->status = status;
}
void threadSetId(Value thread, pthread_t tid) {
    LxThread *th = threadGetInternal(thread);
    th->tid = tid;
}

ThreadStatus threadGetStatus(Value thread) {
    LxThread *th = threadGetInternal(thread);
    return th->status;
}
pthread_t threadGetId(Value thread) {
    LxThread *th = threadGetInternal(thread);
    return th->tid;
}

bool isInstanceLikeObj(Obj *obj) {
    switch (obj->type) {
        case OBJ_T_INSTANCE:
        case OBJ_T_CLASS:
        case OBJ_T_MODULE:
            return true;
        default:
            return false;
    }
}

bool is_obj_function_p(Obj *obj) {
    return obj->type == OBJ_T_FUNCTION;
}
bool is_value_function_p(Value val) {
    return IS_FUNCTION(val);
}
bool is_obj_closure_p(Obj *obj) {
    return obj->type == OBJ_T_CLOSURE;
}
bool is_value_closure_p(Value val) {
    return IS_CLOSURE(val);
}
bool is_obj_native_function_p(Obj *obj) {
    return obj->type == OBJ_T_NATIVE_FUNCTION;
}
bool is_value_native_function_p(Value val) {
    return IS_NATIVE_FUNCTION(val);
}
bool is_obj_class_p(Obj *obj) {
    return obj->type == OBJ_T_CLASS;
}
bool is_value_class_p(Value val) {
    return IS_CLASS(val);
}
bool is_obj_module_p(Obj *obj) {
    return obj->type == OBJ_T_MODULE;
}
bool is_value_module_p(Value val) {
    return IS_MODULE(val);
}
bool is_obj_instance_p(Obj *obj) {
    return obj->type == OBJ_T_INSTANCE;
}
bool is_value_instance_p(Value val) {
    return IS_INSTANCE(val);
}
bool is_obj_bound_method_p(Obj *obj) {
    return obj->type == OBJ_T_BOUND_METHOD;
}
bool is_value_bound_method_p(Value val) {
    return IS_BOUND_METHOD_FUNC(val);
}
bool is_obj_upvalue_p(Obj *obj) {
    return obj->type == OBJ_T_UPVALUE;
}
bool is_value_upvalue_p(Value val) {
    return IS_UPVALUE(val);
}
bool is_obj_internal_p(Obj *obj) {
    return obj->type == OBJ_T_INTERNAL;
}
bool is_value_internal_p(Value val) {
    return IS_INTERNAL(val);
}

bool is_obj_instance_of_p(Obj *obj, ObjClass *klass) {
    return obj->type == OBJ_T_INSTANCE && ((ObjInstance*)obj)->klass == klass;
}
bool is_value_instance_of_p(Value val, ObjClass *klass) {
    return IS_INSTANCE(val) && AS_INSTANCE(val)->klass == klass;
}
bool is_obj_a_p(Obj* obj, ObjClass *klass) {
    return obj->type == OBJ_T_INSTANCE && instanceIsA(((ObjInstance*)obj), klass);
}
bool is_value_a_p(Value val, ObjClass *klass) {
    return IS_INSTANCE(val) && instanceIsA(AS_INSTANCE(val), klass);
}
