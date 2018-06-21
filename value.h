#ifndef clox_value_h
#define clox_value_h

#include <stdio.h>
#include "common.h"

// fwd decls
typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
  VAL_T_BOOL = 1,
  VAL_T_NIL,
  VAL_T_NUMBER,
  VAL_T_OBJ, // includes strings/arrays
  VAL_T_SENTINEL
} ValueType;

typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
    struct Obj *object;
  } as;
} Value;

typedef struct {
    int capacity;
    int count;
    Value *values;
} ValueArray;

#define IS_BOOL(value)    ((value).type == VAL_T_BOOL)
#define IS_NIL(value)     ((value).type == VAL_T_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_T_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_T_OBJ)

#define AS_OBJ(value)     ((value).as.object)
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)

#define BOOL_VAL(value)   ((Value){ VAL_T_BOOL, { .boolean = value } })
#define NIL_VAL           ((Value){ VAL_T_NIL, { .number = 0 } })
#define NUMBER_VAL(value) ((Value){ VAL_T_NUMBER, { .number = value } })
#define OBJ_VAL(obj)      ((Value){ VAL_T_OBJ, { .object = (Obj*)obj } })

#define OBJ_TYPE(value)   (AS_OBJ(value)->type)

void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void freeValueArray(ValueArray *array);
void printValue(FILE *file, Value value, bool canCallMethods);

typedef ObjString *(*newStringFunc)(char *chars, int length);
ObjString *valueToString(Value value, newStringFunc fn);

const char *typeOfVal(Value val);
uint32_t valHash(Value val);
bool valEqual(Value a, Value b);

#endif
