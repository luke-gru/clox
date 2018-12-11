#include <string.h>
#include <stdio.h>
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"
#include "debug.h"
#include "runtime.h"
#include "vec.h"

// allocate object on the VM heap
#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

extern VM vm;
extern bool inCCall;

static Obj *allocateObject(size_t size, ObjType type) {
    ASSERT(vm.inited);
    Obj *object = (Obj*)reallocate(NULL, 0, size);
    ASSERT(type > OBJ_T_NONE);
    object->type = type;
    object->isDark = true;
    object->isFrozen = false;

    // prepend new object to linked list
    object->next = vm.objects;
    if (vm.objects) {
        vm.objects->prev = object;
    }
    object->prev = NULL;
    vm.objects = object;
    object->isLinked = true;
    object->objectId = (size_t)object;

    if (inCCall) {
        vec_push(&vm.stackObjects, object);
    }

    return object;
}

/**
 * Allocate a new lox string object with given characters and length
 * NOTE: length here is strlen(chars). Interns it right away.
 */
static ObjString *allocateString(char *chars, int length, uint32_t hash) {
    if (!vm.inited) {
        fprintf(stderr, "allocateString before VM inited: %s\n", chars);
        ASSERT(vm.inited);
    }
    ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_T_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    return string;
}

void objFreeze(Obj *obj) {
    ASSERT(obj);
    obj->isFrozen = true;
}

bool isFrozen(Obj *obj) {
    ASSERT(obj);
    return obj->isFrozen;
}

uint32_t hashString(char *key, int length) {
    // FNV-1a hash. See: http://www.isthe.com/chongo/tech/comp/fnv/
    uint32_t hash = 2166136261u;

    // This is O(n) on the length of the string, but we only call this when a new
    // string is created. Since the creation is also O(n) (to copy/initialize all
    // the bytes), we allow this here.
    for (int i = 0; i < length; i++) {
        hash ^= key[i];
        hash *= 16777619;
    }

    return hash;
}

// use `*chars` as the underlying storage for the new string object
// NOTE: length here is strlen(chars)
// XXX: Do not pass a static string here, it'll break when we try to free it.
ObjString *takeString(char *chars, int length) {
    DBG_ASSERT(strlen(chars) == length);
    uint32_t hash = hashString(chars, length);
    return allocateString(chars, length, hash);
}

// use copy of `*chars` as the underlying storage for the new string object
// NOTE: length here is strlen(chars)
ObjString *copyString(char *chars, int length) {
    DBG_ASSERT(strlen(chars) >= length);
    // Copy the characters to the heap so the object can own it.
    uint32_t hash = hashString((char*)chars, length);

    char *heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    return allocateString(heapChars, length, hash);
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
        objFreeze((Obj*)interned);
    }
    return interned;
}

ObjString *dupString(ObjString *string) {
    ASSERT(string);
    return copyString(string->chars, string->length);
}

void pushString(ObjString *a, ObjString *b) {
    pushCString(a, b->chars, b->length);
}

// Copies `chars`, adds them to end of string.
// NOTE: don't use this function on a ObjString that is already a key
// for a table, it will fail.
// TODO: add a capacity field to string, so we don't always reallocate when
// pushing new chars to the buffer. Also, treat strings as mutable externally.
// NOTE: length here is strlen(chars)
void pushCString(ObjString *string, char *chars, int lenToAdd) {
    DBG_ASSERT(strlen(chars) >= lenToAdd);
    if (((Obj*)string)->isFrozen) {
        // FIXME: raise FrozenObjectError
        fprintf(stderr, "Tried to modify a frozen string: '%s'\n", string->chars);
        ASSERT(0);
    }
    string->chars = GROW_ARRAY(string->chars, char, string->length+1,
            string->length+lenToAdd+1);
    int i = 0;
    for (i = 0; i < lenToAdd; i++) {
        char *c = chars+i;
        if (c == NULL) break;
        string->chars[string->length + i] = *c;
    }
    string->chars[string->length + i] = '\0';
    string->length += lenToAdd;
    // TODO: avoid rehash, hash should be calculated when needed (lazily)
    string->hash = hashString(string->chars, strlen(string->chars));
}

void pushCStringFmt(ObjString *string, const char *format, ...) {
    if (((Obj*)string)->isFrozen) {
        // FIXME: raise FrozenObjectError
        fprintf(stderr, "Tried to modify a frozen string: '%s'\n", string->chars);
        ASSERT(0);
    }
    char sbuf[201] = {'\0'};
    va_list args;
    va_start(args, format);
    vsnprintf(sbuf, 200, format, args);
    va_end(args);
    size_t buflen = strlen(sbuf);
    sbuf[buflen] = '\0';
    string->chars = GROW_ARRAY(string->chars, char, string->length+1,
            string->length+buflen+1);
    int i = 0;
    for (i = 0; i < buflen; i++) {
        char *c = sbuf+i;
        string->chars[string->length + i] = *c;
    }
    string->chars[string->length + i] = '\0';
    string->length += buflen;
    string->hash = hashString(string->chars, strlen(string->chars));
}

void clearObjString(ObjString *string) {
    if (((Obj*)string)->isFrozen) {
        // FIXME: raise FrozenObjectError
        fprintf(stderr, "Tried to modify a frozen string: '%s'\n", string->chars);
        ASSERT(0);
    }
    string->chars = GROW_ARRAY(string->chars, char, string->length+1, 1);
    string->chars[0] = '\0';
    string->length = 0;
    string->hash = hashString(string->chars, 0);
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
    ASSERT(name);
    ObjClass *klass = ALLOCATE_OBJ(
        ObjClass, OBJ_T_CLASS
    );
    klass->klass = lxClassClass;
    klass->singletonKlass = NULL;
    initTable(&klass->fields);
    initTable(&klass->hiddenFields);
    initTable(&klass->methods);
    initTable(&klass->getters);
    initTable(&klass->setters);
    vec_init(&klass->v_includedMods);
    klass->name = name;
    klass->superclass = superclass;
    return klass;
}

ObjModule *newModule(ObjString *name) {
    ASSERT(name);
    ObjModule *mod = ALLOCATE_OBJ(
        ObjModule, OBJ_T_MODULE
    );
    mod->klass = lxModuleClass;
    mod->singletonKlass = NULL;
    initTable(&mod->fields);
    initTable(&mod->hiddenFields);
    initTable(&mod->methods);
    initTable(&mod->getters);
    initTable(&mod->setters);
    mod->name = name;
    return mod;
}

ObjInstance *newInstance(ObjClass *klass) {
    ASSERT(klass);
    ObjInstance *obj = ALLOCATE_OBJ(
        ObjInstance, OBJ_T_INSTANCE
    );
    obj->klass = klass;
    obj->singletonKlass = NULL;
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

ObjInternal *newInternalObject(void *data, GCMarkFunc markFunc, GCFreeFunc freeFunc) {
    ObjInternal *obj = ALLOCATE_OBJ(
        ObjInternal, OBJ_T_INTERNAL
    );
    obj->data = data;
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

void *internalGetData(ObjInternal *obj) {
    return obj->data;
}

const char *typeOfObj(Obj *obj) {
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
    ObjInstance *instance = newInstance(lxAryClass);
    Value ary = OBJ_VAL(instance);
    lxArrayInit(1, &ary);
    return ary;
}

// duplicates a string instance
Value dupStringInstance(Value instance) {
    ObjString *buf = STRING_GETHIDDEN(instance);
    return newStringInstance(dupString(buf));
}

// creates a new string instance, using `buf` as underlying storage
Value newStringInstance(ObjString *buf) {
    ASSERT(buf);
    ObjInstance *ret = newInstance(lxStringClass);
    Value retVal = OBJ_VAL(ret);
    Value args[2];
    Value bufVal = OBJ_VAL(buf);
    args[0] = retVal;
    args[1] = bufVal;
    lxStringInit(2, args);
    return retVal;
}

void arrayPush(Value self, Value el) {
    ValueArray *ary = ARRAY_GETHIDDEN(self);
    writeValueArray(ary, el);
}

// Deletes the element from the array, returning its old index if
// it was found, otherwise returns -1
int arrayDelete(Value self, Value el) {
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

Value newMap(void) {
    ObjInstance *instance = newInstance(lxMapClass);
    Value map = OBJ_VAL(instance);
    lxMapInit(1, &map);
    return map;
}

bool mapGet(Value mapVal, Value key, Value *ret) {
    Table *map = MAP_GETHIDDEN(mapVal);
    if (tableGet(map, key, ret)) {
        return true;
    } else {
        return false;
    }
}

void mapSet(Value mapVal, Value key, Value val) {
    Table *map = MAP_GETHIDDEN(mapVal);
    tableSet(map, key, val);
}

Value mapSize(Value mapVal) {
    Table *map = MAP_GETHIDDEN(mapVal);
    return NUMBER_VAL(map->count);
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
    ASSERT(tableGet(&inst->hiddenFields, OBJ_VAL(internedString("buf", 3)), &stringVal));
    return (ObjString*)AS_OBJ(stringVal);
}

Value getProp(Value self, ObjString *propName) {
    ASSERT(IS_INSTANCE(self));
    ObjInstance *inst = AS_INSTANCE(self);
    Value ret;
    if (tableGet(&inst->fields, OBJ_VAL(propName), &ret)) {
        return ret;
    } else {
        return NIL_VAL;
    }
}

Value getHiddenProp(Value self, ObjString *propName) {
    ASSERT(IS_INSTANCE(self));
    ObjInstance *inst = AS_INSTANCE(self);
    Value ret;
    if (tableGet(&inst->hiddenFields, OBJ_VAL(propName), &ret)) {
        return ret;
    } else {
        return NIL_VAL;
    }
}

void setProp(Value self, ObjString *propName, Value val) {
    ASSERT(IS_INSTANCE(self));
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

ObjClass *classSingletonClass(ObjClass *klass) {
    if (klass->singletonKlass) {
        return klass->singletonKlass;
    }
    ObjString *name = dupString(klass->name);
    pushCString(name, " (meta)", 7);
    ObjClass *meta = newClass(name, klass->superclass);
    klass->singletonKlass = meta;
    klass->superclass = meta;
    return meta;
}

ObjClass *moduleSingletonClass(ObjModule *mod) {
    if (mod->singletonKlass) {
        return mod->singletonKlass;
    }
    ObjString *name = dupString(mod->name);
    pushCString(name, " (meta)", 7);
    ObjClass *meta = newClass(name, lxClassClass);
    mod->singletonKlass = meta;
    return meta;
}

ObjClass *instanceSingletonClass(ObjInstance *inst) {
    if (inst->singletonKlass) {
        return inst->singletonKlass;
    }
    ObjString *name = valueToString(OBJ_VAL(inst), copyString);
    pushCString(name, " (meta)", 7);
    ObjClass *meta = newClass(name, inst->klass);
    inst->singletonKlass = meta;
    return meta;
}

Value newThread(void) {
    ObjInstance *instance = newInstance(lxThreadClass);
    Value th = OBJ_VAL(instance);
    lxThreadInit(1, &th);
    return th;
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
