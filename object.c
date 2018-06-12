#include <string.h>
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

extern VM vm;

static Obj* allocateObject(size_t size, ObjType type) {
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
static ObjString* allocateString(char *chars, int length) {
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  return string;
}

// use `*chars` as the underlying storage for the new string object
// NOTE: length here is strlen(chars)
ObjString *takeString(char *chars, int length) {
  return allocateString(chars, length);
}

// use copy of `*chars` as the underlying storage for the new string object
// NOTE: length here is strlen(chars)
ObjString *copyString(const char *chars, int length) {
  // Copy the characters to the heap so the object can own it.
  char *heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';

  return allocateString(heapChars, length);
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
}
