#include <stdio.h>

#include "memory.h"
#include "value.h"
#include "object.h"
#include "debug.h"

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
        } else if (OBJ_TYPE(value) == OBJ_FUNCTION) {
            ObjFunction *func = AS_FUNCTION(value);
            if (func->name == NULL) {
                printf("%s", "<fun (Anon)>");
            } else {
                printf("<fun %s>", func->name->chars);
            }
            return;
        }
    }
    printf("Unknown value type: %d. Cannot print!", value.type);
}

ObjString *valueToString(Value value) {
    if (IS_BOOL(value)) {
        if (AS_BOOL(value)) {
            return copyString("true", 4);
        } else {
            return copyString("false", 5);
        }
    } else if (IS_NIL(value)) {
        return copyString("nil", 3);
    } else if (IS_NUMBER(value)) {
        char buftemp[50] = { '\0' };
        double d = AS_NUMBER(value);
        snprintf(buftemp, 50, "%.2f", d); // ex: "1.20"
        char *buf = calloc(strlen(buftemp)+1, 1);
        strcpy(buf, buftemp);
        ASSERT_MEM(buf);
        return takeString(buf, strlen(buf));
    } else if (IS_OBJ(value)) {
        if (OBJ_TYPE(value) == OBJ_STRING) {
            char *cstring = AS_CSTRING(value);
            return copyString(cstring, strlen(cstring));
        } else if (OBJ_TYPE(value) == OBJ_FUNCTION) {
            ObjFunction *func = AS_FUNCTION(value);
            if (func->name == NULL) {
                const char *anon = "<fun (Anon)>";
                return copyString(anon, strlen(anon));
            } else {
                char *buf = calloc(strlen(func->name->chars)+1+6, 1);
                ASSERT_MEM(buf);
                sprintf(buf, "<fun %s>", func->name->chars);
                return takeString(buf, strlen(buf));
            }
        }
    }
    ASSERT(0);
    return NULL;
}
