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
    array->values = NULL;
    initValueArray(array);
}

static void printBool(FILE *file, bool val) {
    fprintf(file, val ? "true" : "false");
}

static void printNumber(FILE *file, double number) {
    fprintf(file, "%g", number);
}

void printValue(FILE *file, Value value) {
    if (IS_BOOL(value)) {
        printBool(file, AS_BOOL(value));
        return;
    } else if (IS_NIL(value)) {
        fprintf(file, "nil");
        return;
    } else if (IS_NUMBER(value)) {
        printNumber(file, AS_NUMBER(value));
        return;
    } else if (IS_OBJ(value)) {
        if (OBJ_TYPE(value) == OBJ_STRING) {
            char *cstring = AS_CSTRING(value);
            fprintf(file, "%s", cstring);
            return;
        } else if (OBJ_TYPE(value) == OBJ_FUNCTION) {
            ObjFunction *func = AS_FUNCTION(value);
            if (func->name == NULL) {
                fprintf(file, "%s", "<fun (Anon)>");
            } else {
                fprintf(file, "<fun %s>", func->name->chars);
            }
            return;
        } else if (OBJ_TYPE(value) == OBJ_INSTANCE) {
            ObjClass *klass = AS_INSTANCE(value)->klass;
            char *klassName = klass->name->chars;
            fprintf(file, "<instance %s>", klassName);
            return;
        } else if (OBJ_TYPE(value) == OBJ_CLASS) {
            ObjClass *klass = AS_CLASS(value);
            char *klassName = klass->name->chars;
            fprintf(file, "<class %s>", klassName);
            return;
        } else if (OBJ_TYPE(value) == OBJ_NATIVE_FUNCTION) {
            ObjNative *native = AS_NATIVE_FUNCTION(value);
            ObjString *name = native->name;
            fprintf(file, "<fn %s (native)>", name->chars);
            return;
        } else if (OBJ_TYPE(value) == OBJ_BOUND_METHOD) {
            ObjBoundMethod *bmethod = AS_BOUND_METHOD(value);
            ObjString *name = bmethod->method->name;
            fprintf(file, "<method %s>", name->chars);
            return;
        }
    }
    fprintf(file, "Unknown value type: %d. Cannot print!\n", value.type);
    ASSERT(0);
}

// returns a ObjString hidden from the GC
ObjString *valueToString(Value value) {
    turnGCOff();
    ObjString *ret = NULL;
    if (IS_BOOL(value)) {
        if (AS_BOOL(value)) {
            ret = copyString("true", 4);
        } else {
            ret = copyString("false", 5);
        }
    } else if (IS_NIL(value)) {
        ret = copyString("nil", 3);
    } else if (IS_NUMBER(value)) {
        char buftemp[50] = { '\0' };
        double d = AS_NUMBER(value);
        snprintf(buftemp, 50, "%.2f", d); // ex: "1.20"
        char *buf = calloc(strlen(buftemp)+1, 1);
        strcpy(buf, buftemp);
        ASSERT_MEM(buf);
        ret = takeString(buf, strlen(buf));
    } else if (IS_OBJ(value)) {
        if (OBJ_TYPE(value) == OBJ_STRING) {
            char *cstring = AS_CSTRING(value);
            ret = copyString(cstring, strlen(cstring));
        } else if (OBJ_TYPE(value) == OBJ_FUNCTION) {
            ObjFunction *func = AS_FUNCTION(value);
            if (func->name == NULL) {
                const char *anon = "<fun (Anon)>";
                ret = copyString(anon, strlen(anon));
            } else {
                char *buf = calloc(strlen(func->name->chars)+1+6, 1);
                ASSERT_MEM(buf);
                sprintf(buf, "<fun %s>", func->name->chars);
                ret = takeString(buf, strlen(buf));
            }
        } else if (OBJ_TYPE(value) == OBJ_INSTANCE) {
            ObjClass *klass = AS_INSTANCE(value)->klass;
            char *klassName = klass->name->chars;
            char *cbuf = calloc(strlen(klassName)+1+11, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<instance %s>", klassName);
            ret = takeString(cbuf, strlen(cbuf));
        } else if (OBJ_TYPE(value) == OBJ_CLASS) {
            ObjClass *klass = AS_CLASS(value);
            char *klassName = klass->name->chars;
            char *cbuf = calloc(strlen(klassName)+1+8, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<class %s>", klassName);
            ret = takeString(cbuf, strlen(cbuf));
        } else if (OBJ_TYPE(value) == OBJ_NATIVE_FUNCTION) {
            ObjNative *native = AS_NATIVE_FUNCTION(value);
            ObjString *name = native->name;
            char *nameStr = name->chars;
            char *cbuf = calloc(strlen(nameStr)+1+14, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<fn %s (native)>", nameStr);
            ret = takeString(cbuf, strlen(cbuf));
        } else if (OBJ_TYPE(value) == OBJ_BOUND_METHOD) {
            ObjBoundMethod *bmethod = AS_BOUND_METHOD(value);
            ObjString *name = bmethod->method->name;
            char *nameStr = name->chars;
            char *cbuf = calloc(strlen(nameStr)+1+9, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<method %s>", nameStr);
            ret = takeString(cbuf, strlen(cbuf));
        }
    }
    if (ret) {
        hideFromGC((Obj*)ret);
        turnGCOn();
        return ret;
    }
    ASSERT(0);
    return NULL;
}

const char *typeOfVal(Value val) {
    if (IS_OBJ(val)) {
        return typeOfObj(AS_OBJ(val));
    } else {
        if (IS_BOOL(val)) return "bool";
        if (IS_NIL(val)) return "nil";
        if (IS_NUMBER(val)) return "number";
    }
    fprintf(stderr, "Unknown value type! Pointer: %p\n", AS_OBJ(val));
    ASSERT(0);
    return "unknown!";
}
