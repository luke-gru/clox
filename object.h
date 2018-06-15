#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"

typedef enum {
  OBJ_STRING,
  OBJ_FUNCTION,
} ObjType;

typedef struct sObj {
  ObjType type;
  struct sObj *next;
} Obj;

typedef struct sObjString {
  struct sObj object;
  int length;
  char *chars;
  uint32_t hash;
} ObjString;

typedef struct sObjFunction {
  Obj object;
  int arity;
  //int upvalueCount;
  Chunk *chunk;
  ObjString *name;
} ObjFunction;

#define IS_STRING(value)        isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value)      isObjType(value, OBJ_FUNCTION)

#define AS_STRING(value)        ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)       (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value)      ((ObjFunction*)AS_OBJ(value))

ObjString *takeString(char *chars, int length);
ObjString *copyString(const char *chars, int length);
ObjFunction *newFunction();
void freeFunction(ObjFunction *func);
void pushCString(ObjString *string, char *chars, int lenToAdd);

// Returns true if [value] is an object of type [type]. Do not call this
// directly, instead use the [IS___] macro for the type in question.
static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
