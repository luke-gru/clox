#include <stdio.h>

#include "memory.h"
#include "value.h"
#include "object.h"

void initValueArray(ValueArray *array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray *array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(
            array->values, Value,
            oldCapacity, array->capacity
        );
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray *array) {
    FREE_ARRAY(ValueArray, array->values, array->capacity);
    initValueArray(array);
}

static void printBool(bool val) {
    printf(val ? "true" : "false");
}

static void printNumber(double number) {
    printf("%g", number);
}

void printValue(Value value) {
    if (IS_BOOL(value)) {
        printBool(AS_BOOL(value));
        return;
    } else if (IS_NIL(value)) {
        printf("nil");
        return;
    } else if (IS_NUMBER(value)) {
        printNumber(AS_NUMBER(value));
        return;
    } else if (IS_OBJ(value)) {
        if (OBJ_TYPE(value) == OBJ_STRING) {
            char *cstring = AS_CSTRING(value);
            printf("%s", cstring);
            return;
        }
    }
    printf("Unknown value type: %d. Cannot print!", value.type);
}
