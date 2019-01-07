#ifndef clox_value_h
#define clox_value_h

#include <stdio.h> // for FILE struct
#include "common.h"
#include "vec.h"

// fwd decls
typedef struct Obj Obj;
typedef struct ObjString ObjString;

#ifdef NAN_TAGGING
typedef uint64_t Value;

// A mask that selects the sign bit.
#define SIGN_BIT ((uint64_t)1 << 63)

// The bits that must be set to indicate a quiet NaN.
#define QNAN ((uint64_t)0x7ffc000000000000)

// If the NaN bits are set, it's not a number.
#define IS_BOOL(value)   ((value) == TRUE_VAL || (value) == FALSE_VAL)
#define IS_NIL(value)    ((value) == NIL_VAL)
#define IS_NUMBER(value) (((value) & QNAN) != QNAN)
#define IS_OBJ(value)    (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))
#define IS_UNDEF(value)  ((value) == UNDEF_VAL)

// Masks out the tag bits used to identify the singleton value.
#define MASK_TAG (7)

// Tag values for the different singleton values.
#define TAG_NAN       (0)
#define TAG_NIL       (1)
#define TAG_FALSE     (2)
#define TAG_TRUE      (3)
#define TAG_UNDEFINED (4)
#define TAG_UNUSED2   (5)
#define TAG_UNUSED3   (6)
#define TAG_UNUSED4   (7)

// Value -> 0 or 1.
#define AS_BOOL(value)    ((value) == TRUE_VAL)
// Value -> Obj*.
#define AS_OBJ(value)     ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))
#define AS_NUMBER(value)  (valueToNumber(value))

// Singleton values.

#define BOOL_VAL(b)   ((b) ? TRUE_VAL : FALSE_VAL)
#define TRUE_VAL      ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define NIL_VAL       ((Value)(uint64_t)(QNAN | TAG_NIL))
#define FALSE_VAL     ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define UNDEF_VAL     ((Value)(uint64_t)(QNAN | TAG_UNDEFINED))
#define NUMBER_VAL(n) (numberToValue((double)(n)))
#define OBJ_VAL(obj)  (objectToValue((Obj*)(obj)))

// Gets the singleton type tag for a Value (which must be a singleton).
#define GET_TAG(value) ((int)((value) & MASK_TAG))
#else
typedef enum {
    VAL_T_TRUE = 1,
    VAL_T_FALSE,
    VAL_T_NIL,
    VAL_T_NUMBER,
    VAL_T_OBJ, // includes strings/arrays
    VAL_T_UNDEF // used as an 'undefined' value type, for example as an undefined hash key
} ValueType;

typedef struct Value {
    ValueType type;
    union {
        double number;
        struct Obj *object;
    } as;
} Value;

#define IS_BOOL(value)    ((value).type == VAL_T_TRUE || (value).type == VAL_T_FALSE)
#define IS_NIL(value)     ((value).type == VAL_T_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_T_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_T_OBJ)
#define IS_UNDEF(value)   ((value).type == VAL_T_UNDEF)

#define AS_OBJ(value)     ((value).as.object)
#define AS_BOOL(value)    ((value).as.number == 0 ? false : true)
#define AS_NUMBER(value)  ((value).as.number)

#define BOOL_VAL(b)   (b ? TRUE_VAL : FALSE_VAL)
#define TRUE_VAL      ((Value){ VAL_T_TRUE, { .number = 1 } })
#define FALSE_VAL     ((Value){ VAL_T_FALSE, { .number = 0 } })
#define NIL_VAL       ((Value){ VAL_T_NIL, { .number = 0 } } )
#define UNDEF_VAL     ((Value){ VAL_T_UNDEF, { .number = -1 } })
#define NUMBER_VAL(n) ((Value){ VAL_T_NUMBER, { .number = n } })
#define OBJ_VAL(obj)  ((Value){ VAL_T_OBJ, { .object = (Obj*)obj } })

#endif

typedef vec_t(Value) vec_val_t;

typedef struct ValueArray {
    int capacity;
    int count;
    Value *values;
} ValueArray;

#ifdef NAN_TAGGING
#define VALARRAY_FOREACH(ary, val, idx) \
    for (idx = 0; idx < ary->count && (val = ary->values[idx]) && !IS_UNDEF(val); idx++)

#define VALARRAY_FOREACH_START(ary, val, startIdx, idx) \
    for (idx = startIdx; idx < ary->count && (val = ary->values[idx]) && !IS_UNDEF(val); idx++)
#else
#define VALARRAY_FOREACH(ary, val, idx) \
    for (idx = 0; idx < ary->count && (val = ary->values[idx]).type != VAL_T_UNDEF; idx++)

#define VALARRAY_FOREACH_START(ary, val, startIdx, idx) \
    for (idx = startIdx; idx < ary->count && (val = ary->values[idx]).type != VAL_T_UNDEF; idx++)
#endif

#define IS_BOOL_FUNC (is_bool_p)
#define IS_NIL_FUNC (is_nil_p)
#define IS_NUMBER_FUNC (is_number_p)
#define IS_OBJ_FUNC (is_obj_p)

#define OBJ_TYPE(value)   (AS_OBJ(value)->type)

// A union to let us reinterpret a double as raw bits and back.
typedef union
{
  uint64_t bits64;
  uint32_t bits32[2];
  double num;
} DoubleBits;

static inline double valueToNumber(Value val) {
#if NAN_TAGGING
  DoubleBits data;
  data.bits64 = val;
  return data.num;
#else
  return val.as.number;
#endif
}

static inline Value objectToValue(Obj *obj) {
#if NAN_TAGGING
  // The triple casting is necessary here to satisfy some compilers:
  // 1. (uintptr_t) Convert the pointer to a number of the right size.
  // 2. (uint64_t)  Pad it up to 64 bits in 32-bit builds.
  // 3. Or in the bits to make a tagged Nan.
  // 4. Cast to a typedef'd value.
  return (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj));
#else
  Value value;
  value.type = VAL_T_OBJ;
  value.as.object = obj;
  return value;
#endif
}

static inline Value numberToValue(double num) {
#if NAN_TAGGING
  DoubleBits data;
  data.num = num;
  return data.bits64;
#else
  Value value;
  value.type = VAL_T_NUMBER;
  value.as.number = num;
  return value;
#endif
}

void initValueArray(ValueArray *array);
void initValueArrayWithCapa(ValueArray *array, int capa);
void writeValueArrayEnd(ValueArray *array, Value value);
void writeValueArrayBeg(ValueArray *array, Value value);
void writeValueArrayBulk(ValueArray *array, size_t offset, size_t num, Value value);

void freeValueArray(ValueArray *array);
bool removeValueArray(ValueArray *array, int idx);
int printValue(FILE *file, Value value, bool canCallMethods, int maxLen);

// value type predicate function
typedef bool (*value_type_p)(Value val);
bool is_bool_p(Value);
bool is_nil_p(Value);
bool is_number_p(Value);
bool is_obj_p(Value);

typedef ObjString *(*newStringFunc)(char *chars, int length, int flags);
ObjString *valueToString(Value value, newStringFunc fn, int flags);

const char *typeOfVal(Value val);
uint32_t valHash(Value val);
bool valEqual(Value a, Value b);
bool isTruthy(Value a);
void fillCallableName(Value callable, const char buf[], size_t buflen);

#endif
