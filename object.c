#include <string.h>
#include <stdio.h>
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"
#include "debug.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

extern VM vm;

static Obj *allocateObject(size_t size, ObjType type) {
    Obj *object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->isDark = true;

    object->next = vm.objects;
    vm.objects = object;

    return object;
}

/**
 * Allocate a new lox string object with given characters and length
 * NOTE: length here is strlen(chars)
 */
static ObjString *allocateString(char *chars, int length, uint32_t hash) {
    ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    hideFromGC((Obj*)string);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    tableSet(&vm.strings, string, NIL_VAL);
    unhideFromGC((Obj*)string);
    return string;
}

static uint32_t hashString(char *key, int length) {
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
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;
    return allocateString(chars, length, hash);
}

// use copy of `*chars` as the underlying storage for the new string object
// NOTE: length here is strlen(chars)
ObjString *copyString(const char *chars, int length) {
    // Copy the characters to the heap so the object can own it.
    uint32_t hash = hashString((char*)chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    char *heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    return allocateString(heapChars, length, hash);
}

ObjString *internedString(const char *chars) {
    int length = (int)strlen(chars);
    uint32_t hash = hashString((char*)chars, length);
    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
    ASSERT(interned);
    return interned;
}

void freeString(ObjString *string) {
    freeObject((Obj*)string);
}

// TODO: add a capacity field to string, so we don't always reallocate when
// pushing new chars to the buffer.
// NOTE: length here is strlen(chars)
void pushCString(ObjString *string, char *chars, int lenToAdd) {
    /*fprintf(stderr, "pushCSTring\n");*/
    string->chars = GROW_ARRAY(string->chars, char, string->length,
            string->length+lenToAdd+1);
    int i = 0;
    for (i = 0; i < lenToAdd; i++) {
        char *c = chars+i;
        if (c == NULL) break;
        string->chars[string->length + i] = *c;
    }
    string->chars[string->length + i] = '\0';
    string->length += lenToAdd;
    // TODO: avoid rehash, hash should be calculated when needed!
    string->hash = hashString(string->chars, strlen(string->chars));
    /*fprintf(stderr, "/pushCSTring\n");*/
}

ObjFunction *newFunction(Chunk *chunk) {
    ObjFunction *function = ALLOCATE_OBJ(
        ObjFunction, OBJ_FUNCTION
    );

    function->arity = 0;
    /*function->upvalueCount = 0;*/
    function->name = NULL;
    if (chunk == NULL) {
        initChunk(&function->chunk);
    } else {
        function->chunk = *chunk; // copy
    }
    return function;
}

void freeFunction(ObjFunction *func) {
    // TODO: free objstring if not null
    freeChunk(&func->chunk);
    FREE(ObjFunction, func);
}

ObjClass *newClass(ObjString *name, ObjClass *superclass) {
    ASSERT(name);
    ObjClass *klass = ALLOCATE_OBJ(
        ObjClass, OBJ_CLASS
    );
    initTable(&klass->methods);
    klass->name = name;
    klass->superclass = superclass;
    return klass;
}

ObjInstance *newInstance(ObjClass *klass) {
    ASSERT(klass);
    ObjInstance *obj = ALLOCATE_OBJ(
        ObjInstance, OBJ_INSTANCE
    );
    obj->klass = klass;
    initTable(&obj->fields);
    return obj;
}

ObjNative *newNative(ObjString *name, NativeFn function) {
    ASSERT(function);
    ObjNative *native = ALLOCATE_OBJ(
        ObjNative, OBJ_NATIVE_FUNCTION
    );
    native->function = function;
    native->name = name;
    return native;
}

ObjBoundMethod *newBoundMethod(ObjInstance *receiver, ObjFunction *method) {
    ASSERT(receiver);
    ASSERT(method);
    ObjBoundMethod *bmethod = ALLOCATE_OBJ(
        ObjBoundMethod, OBJ_BOUND_METHOD
    );
    bmethod->receiver = OBJ_VAL(receiver);
    bmethod->method = method;
    return bmethod;
}

const char *typeOfObj(Obj *obj) {
    switch (obj->type) {
    case OBJ_STRING:
        return "string";
    case OBJ_CLASS:
        return "class";
    case OBJ_INSTANCE:
        return "instance";
    case OBJ_FUNCTION:
    case OBJ_NATIVE_FUNCTION:
    case OBJ_BOUND_METHOD:
        return "function";
    default:
        ASSERT(0);
        return "unknown";
    }
}
