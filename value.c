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
        fprintf(stderr, "printing bool\n");
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

int serializeValue(Value *value, FILE *file, int *errcode) {
    int written = 0;
    int one = 1;
    if (IS_NIL(*value)) {
        written = fwrite(&one, sizeof(int), 1, file);
        char n = 'n';
        written += fwrite(&n, 1, 1, file);
        return written;
    }
    if (IS_BOOL(*value)) {
        written = fwrite(&one, sizeof(int), 1, file);
        if (AS_BOOL(*value)) {
            char t = 't';
            written += fwrite(&t, 1, 1, file);
        } else {
            char f = 'f';
            written += fwrite(&f, 1, 1, file);
        }
        return written;
    }
    if (IS_NUMBER(*value)) {
        double d = AS_NUMBER(*value);
        int sd = sizeof(double);
        written = fwrite(&sd, sizeof(int), 1, file);
        char cd = 'd';
        written += fwrite(&cd, 1, 1, file);
        written += fwrite(&d, sizeof(double), 1, file);
        return written;
    }
    if (IS_OBJ(*value)) {
        switch(OBJ_TYPE(*value)) {
        case OBJ_STRING: {
            char *str = AS_CSTRING(*value);
            int len = (int)strlen(str);
            written = fwrite(&len, sizeof(int), 1, file);
            char s = 's';
            written += fwrite(&s, 1, 1, file);
            written += fwrite(str, len, 1, file);
            return written;
        }
        case OBJ_FUNCTION: {
            ObjFunction *func = AS_FUNCTION(*value);
            char *name = func->name ? func->name->chars : "";
            int namelen = strlen(name)+1;
            written = fwrite(&namelen, sizeof(int), 1, file);
            char c = 'c';
            written += fwrite(&c, 1, 1, file);
            written += fwrite(&func->arity, sizeof(int), 1, file);
            written += fwrite(name, namelen, 1, file);
            ASSERT(func->chunk.code != NULL);
            ASSERT(func->chunk.lines != NULL);
            fprintf(stderr, "serializing inner function with %d bytes of code\n", func->chunk.count);
            int serRes = serializeChunk(&func->chunk, file, errcode);
            if (serRes != 0) {
                fprintf(stderr, "serializeValue failure 0\n");
            }
            return written;
        }
        default:
            fprintf(stderr, "serializeValue failure 1\n");
            return -1;
        }
    }
    fprintf(stderr, "serializeValue failure 2\n");
    return -1;
}
