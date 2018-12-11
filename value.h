#ifndef clox_value_h
#define clox_value_h

#include <stdio.h>
#include "common.h"
#include "vec.h"

// fwd decls
typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
  VAL_T_BOOL = 1,
  VAL_T_NIL,
  VAL_T_NUMBER,
  VAL_T_OBJ, // includes strings/arrays
  VAL_T_UNDEF // used as an 'undefined' value type, for example as an undefined hash key
} ValueType;

typedef struct Value {
  ValueType type;
  union {
    bool boolean;
    double number;
    struct Obj *object;
  } as;
} Value;

typedef vec_t(Value) vec_val_t;

typedef struct ValueArray {
    int capacity;
    int count;
    Value *values;
} ValueArray;

#define VALARRAY_FOREACH(ary, val, idx) \
    for (idx = 0; idx < ary->count; idx++)


#define IS_BOOL(value)    ((value).type == VAL_T_BOOL)
#define IS_NIL(value)     ((value).type == VAL_T_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_T_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_T_OBJ)
#define IS_UNDEF(value)   ((value).type == VAL_T_UNDEF)

#define IS_BOOL_FUNC (is_bool_p)
#define IS_NIL_FUNC (is_nil_p)
#define IS_NUMBER_FUNC (is_number_p)
#define IS_OBJ_FUNC (is_obj_p)

#define AS_OBJ(value)     ((value).as.object)
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)

#define BOOL_VAL(b)   ((Value){ VAL_T_BOOL, { .boolean = b } })
#define NIL_VAL       ((Value){ VAL_T_NIL, { .number = 0 } })
#define UNDEF_VAL     ((Value){ VAL_T_UNDEF, { .number = -1 } })
#define NUMBER_VAL(n) ((Value){ VAL_T_NUMBER, { .number = n } })
#define OBJ_VAL(obj)  ((Value){ VAL_T_OBJ, { .object = (Obj*)obj } })

#define OBJ_TYPE(value)   (AS_OBJ(value)->type)

void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void freeValueArray(ValueArray *array);
void removeValueArray(ValueArray *array, int idx);
void printValue(FILE *file, Value value, bool canCallMethods);

// value type predicate function
typedef bool (*value_type_p)(Value val);
bool is_bool_p(Value);
bool is_nil_p(Value);
bool is_number_p(Value);
bool is_obj_p(Value);

typedef ObjString *(*newStringFunc)(char *chars, int length);
ObjString *valueToString(Value value, newStringFunc fn);

const char *typeOfVal(Value val);
uint32_t valHash(Value val);
bool valEqual(Value a, Value b);

#endif
