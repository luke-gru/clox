#include <string.h>
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

extern VM vm;

static Obj *allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;

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
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    tableSet(&vm.strings, string, NIL_VAL);
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
ObjString *takeString(char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;
    return allocateString(chars, length, hash);
}

// use copy of `*chars` as the underlying storage for the new string object
// NOTE: length here is strlen(chars)
ObjString *copyString(const char *chars, int length) {
    // Copy the characters to the heap so the object can own it.
    char *heapChars = ALLOCATE(char, length + 1);
    uint32_t hash = hashString((char*)chars, length);

    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    return allocateString(heapChars, length, hash);
}

// TODO: add a capacity field to string, so we don't always reallocate when
// pushing new chars to the buffer.
// NOTE: length here is strlen(chars)
void pushCString(ObjString *string, char *chars, int lenToAdd) {
    string->chars = GROW_ARRAY(string->chars, char, string->length,
            string->length+lenToAdd);
    for (int i = 0; i < lenToAdd; i++) {
        char *c = chars+i;
        if (c == NULL) break;
        string->chars[string->length + i] = *c;
    }
    string->length += lenToAdd;
    // TODO: avoid rehash, hash should be calculated when needed!
    string->hash = hashString(string->chars, strlen(string->chars));
}
