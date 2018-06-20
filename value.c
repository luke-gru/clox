#include <stdio.h>

#include "memory.h"
#include "value.h"
#include "object.h"
#include "debug.h"
#include "vm.h"

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

void printValue(FILE *file, Value value, bool canCallMethods) {
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
        if (OBJ_TYPE(value) == OBJ_T_STRING) {
            char *cstring = AS_CSTRING(value);
            fprintf(file, "%s", cstring ? cstring : "(NULL)");
            return;
        } else if (OBJ_TYPE(value) == OBJ_T_FUNCTION) {
            ObjFunction *func = AS_FUNCTION(value);
            if (func->name == NULL) {
                fprintf(file, "%s", "<fun (Anon)>");
            } else {
                fprintf(file, "<fun %s>", func->name->chars);
            }
            return;
        } else if (OBJ_TYPE(value) == OBJ_T_INSTANCE) {
            ObjInstance *inst = AS_INSTANCE(value);
            Obj *callable = instanceFindMethod(inst, copyString("toString", 8));
            if (callable && vm.inited && canCallMethods) {
                Value stringVal = callVMMethod(inst, OBJ_VAL(callable), 0, NULL);
                if (!IS_STRING(stringVal)) {
                    runtimeError("TypeError, toString() returned non-string");
                    return;
                }
                ObjString *out = AS_STRING(stringVal);
                fprintf(file, "%s", out->chars);
                ASSERT(AS_OBJ(pop()) == AS_OBJ(stringVal));;
            } else {
                ObjClass *klass = inst->klass;
                char *klassName = klass->name->chars;
                fprintf(file, "<instance %s>", klassName);
            }
            return;
        } else if (OBJ_TYPE(value) == OBJ_T_CLASS) {
            ObjClass *klass = AS_CLASS(value);
            char *klassName = klass->name->chars;
            fprintf(file, "<class %s>", klassName);
            return;
        } else if (OBJ_TYPE(value) == OBJ_T_NATIVE_FUNCTION) {
            ObjNative *native = AS_NATIVE_FUNCTION(value);
            ObjString *name = native->name;
            fprintf(file, "<fn %s (native)>", name->chars);
            return;
        } else if (OBJ_TYPE(value) == OBJ_T_BOUND_METHOD) {
            ObjBoundMethod *bmethod = AS_BOUND_METHOD(value);
            ObjString *name;
            if (bmethod->callable->type == OBJ_T_FUNCTION) {
                ObjFunction *func = (ObjFunction*)bmethod->callable;
                name = func->name;
            } else if (bmethod->callable->type == OBJ_T_NATIVE_FUNCTION) {
                ObjNative *func = (ObjNative*)bmethod->callable;
                name = func->name;
            } else {
                ASSERT(0);
            }
            fprintf(file, "<method %s>", name->chars);
            return;
        } else if (OBJ_TYPE(value) == OBJ_T_INTERNAL) {
            fprintf(file, "<internal>");
            return;
        }
    }
    fprintf(file, "Unknown value type: %d. Cannot print!\n", value.type);
    ASSERT(0);
}

// returns an ObjString hidden from the GC
ObjString *valueToString(Value value) {
    ObjString *ret = NULL;
    if (IS_BOOL(value)) {
        if (AS_BOOL(value)) {
            ret = newString("true", 4);
        } else {
            ret = newString("false", 5);
        }
    } else if (IS_NIL(value)) {
        ret = newString("nil", 3);
    } else if (IS_NUMBER(value)) {
        char buftemp[50] = { '\0' };
        double d = AS_NUMBER(value);
        snprintf(buftemp, 50, "%.2f", d); // ex: "1.20"
        char *buf = calloc(strlen(buftemp)+1, 1);
        ASSERT_MEM(buf);
        strcpy(buf, buftemp);
        ret = newString(buf, strlen(buf));
    } else if (IS_OBJ(value)) {
        if (OBJ_TYPE(value) == OBJ_T_STRING) {
            char *cstring = AS_CSTRING(value);
            ASSERT(cstring);
            ret = newString(strdup(cstring), strlen(cstring));
        } else if (OBJ_TYPE(value) == OBJ_T_FUNCTION) {
            ObjFunction *func = AS_FUNCTION(value);
            if (func->name == NULL) {
                const char *anon = "<fun (Anon)>";
                ret = newString(anon, strlen(anon));
            } else {
                char *buf = calloc(strlen(func->name->chars)+1+6, 1);
                ASSERT_MEM(buf);
                sprintf(buf, "<fun %s>", func->name->chars);
                ret = newString(buf, strlen(buf));
                /*free(buf);*/
            }
        } else if (OBJ_TYPE(value) == OBJ_T_INSTANCE) {
            ObjClass *klass = AS_INSTANCE(value)->klass;
            char *klassName = klass->name->chars;
            char *cbuf = calloc(strlen(klassName)+1+11, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<instance %s>", klassName);
            ret = newString(cbuf, strlen(cbuf));
            /*free(cbuf);*/
        } else if (OBJ_TYPE(value) == OBJ_T_CLASS) {
            ObjClass *klass = AS_CLASS(value);
            char *klassName = klass->name->chars;
            char *cbuf = calloc(strlen(klassName)+1+8, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<class %s>", klassName);
            ret = newString(cbuf, strlen(cbuf));
            /*free(cbuf);*/
        } else if (OBJ_TYPE(value) == OBJ_T_NATIVE_FUNCTION) {
            ObjNative *native = AS_NATIVE_FUNCTION(value);
            ObjString *name = native->name;
            char *nameStr = name->chars;
            char *cbuf = calloc(strlen(nameStr)+1+14, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<fn %s (native)>", nameStr);
            ret = newString(cbuf, strlen(cbuf));
            /*free(cbuf);*/
        } else if (OBJ_TYPE(value) == OBJ_T_BOUND_METHOD) {
            ObjBoundMethod *bmethod = AS_BOUND_METHOD(value);
            ObjString *name;
            if (bmethod->callable->type == OBJ_T_FUNCTION) {
                name = ((ObjFunction*)(bmethod->callable))->name;
            } else if (bmethod->callable->type == OBJ_T_NATIVE_FUNCTION) {
                name = ((ObjNative*)(bmethod->callable))->name;
            } else {
                ASSERT(0);
            }
            char *nameStr = name->chars;
            char *cbuf = calloc(strlen(nameStr)+1+9, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<method %s>", nameStr);
            ret = newString(cbuf, strlen(cbuf));
            /*free(cbuf);*/
        }
    }
    if (ret) {
        hideFromGC((Obj*)ret);
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
