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
        if (IS_STRING(value)) {
            char *cstring = AS_CSTRING(value);
            fprintf(file, "%s", cstring ? cstring : "(NULL)");
            return;
        } else if (IS_FUNCTION(value) || IS_CLOSURE(value)) {
            ObjFunction *func = NULL;
            if (IS_FUNCTION(value)) {
                func = AS_FUNCTION(value);
            } else {
                func = AS_CLOSURE(value)->function;
            }
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
            if (bmethod->callable->type == OBJ_T_CLOSURE) {
                ObjFunction *func = ((ObjClosure*)bmethod->callable)->function;
                name = func->name;
            } else if (bmethod->callable->type == OBJ_T_NATIVE_FUNCTION) {
                ObjNative *func = (ObjNative*)bmethod->callable;
                name = func->name;
            } else {
                UNREACHABLE("BUG");
            }
            ASSERT(name->chars);
            fprintf(file, "<method %s>", name->chars);
            return;
        } else if (OBJ_TYPE(value) == OBJ_T_INTERNAL) {
            fprintf(file, "<internal>");
            return;
        } else {
            UNREACHABLE("Unknown object type: valtype=%s (objtype=%d)",
                typeOfVal(value),
                AS_OBJ(value)->type
            );
        }
    }
    fprintf(file, "Unknown value type: %d. Cannot print!\n", value.type);
    UNREACHABLE("BUG");
}

// returns an ObjString hidden from the GC
ObjString *valueToString(Value value, newStringFunc stringConstructor) {
    ASSERT(stringConstructor != takeString); // should copy the constructed c string
    ObjString *ret = NULL;
    if (IS_BOOL(value)) {
        if (AS_BOOL(value)) {
            ret = stringConstructor("true", 4);
        } else {
            ret = stringConstructor("false", 5);
        }
    } else if (IS_NIL(value)) {
        ret = stringConstructor("nil", 3);
    } else if (IS_NUMBER(value)) {
        char buftemp[50] = { '\0' };
        double d = AS_NUMBER(value);
        snprintf(buftemp, 50, "%.2f", d); // ex: "1.20"
        char *buf = calloc(strlen(buftemp)+1, 1);
        ASSERT_MEM(buf);
        strcpy(buf, buftemp);
        ret = stringConstructor(buf, strlen(buf));
        free(buf);
    } else if (IS_OBJ(value)) {
        if (OBJ_TYPE(value) == OBJ_T_STRING) {
            char *cstring = AS_CSTRING(value);
            ASSERT(cstring);
            ret = stringConstructor(cstring, strlen(cstring));
        } else if (IS_FUNCTION(value) || IS_CLOSURE(value)) {
            ObjFunction *func = NULL;
            if (IS_FUNCTION(value)) {
                func = AS_FUNCTION(value);
            } else {
                func = AS_CLOSURE(value)->function;
            }
            if (func->name == NULL) {
                const char *anon = "<fun (Anon)>";
                ret = stringConstructor(anon, strlen(anon));
            } else {
                char *buf = calloc(strlen(func->name->chars)+1+6, 1);
                ASSERT_MEM(buf);
                sprintf(buf, "<fun %s>", func->name->chars);
                ret = stringConstructor(buf, strlen(buf));
                free(buf);
            }
        } else if (OBJ_TYPE(value) == OBJ_T_INSTANCE) {
            ObjInstance *inst = AS_INSTANCE(value);
            Obj *toString = instanceFindMethod(inst, copyString("toString", 8));
            if (toString && vm.inited) {
                Value stringVal = callVMMethod(inst, OBJ_VAL(toString), 0, NULL);
                if (!IS_STRING(stringVal)) {
                    runtimeError("TypeError, toString() returned non-string");
                    UNREACHABLE("error");
                }
                ret = AS_STRING(stringVal);
            } else {
                ObjClass *klass = inst->klass;
                char *klassName = klass->name->chars;
                char *cbuf = calloc(strlen(klassName)+1+11, 1);
                ASSERT_MEM(cbuf);
                sprintf(cbuf, "<instance %s>", klassName);
                ret = stringConstructor(cbuf, strlen(cbuf));
                free(cbuf);
            }
        } else if (OBJ_TYPE(value) == OBJ_T_CLASS) {
            ObjClass *klass = AS_CLASS(value);
            char *klassName = klass->name->chars;
            char *cbuf = calloc(strlen(klassName)+1+8, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<class %s>", klassName);
            ret = stringConstructor(cbuf, strlen(cbuf));
            free(cbuf);
        } else if (OBJ_TYPE(value) == OBJ_T_NATIVE_FUNCTION) {
            ObjNative *native = AS_NATIVE_FUNCTION(value);
            ObjString *name = native->name;
            char *nameStr = name->chars;
            char *cbuf = calloc(strlen(nameStr)+1+14, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<fn %s (native)>", nameStr);
            ret = stringConstructor(cbuf, strlen(cbuf));
            free(cbuf);
        } else if (OBJ_TYPE(value) == OBJ_T_BOUND_METHOD) {
            ObjBoundMethod *bmethod = AS_BOUND_METHOD(value);
            ObjString *name;
            if (bmethod->callable->type == OBJ_T_FUNCTION) {
                name = ((ObjFunction*)(bmethod->callable))->name;
            } else if (bmethod->callable->type == OBJ_T_NATIVE_FUNCTION) {
                name = ((ObjNative*)(bmethod->callable))->name;
            } else {
                UNREACHABLE("error");
            }
            char *nameStr = name->chars;
            char *cbuf = calloc(strlen(nameStr)+1+9, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<method %s>", nameStr);
            ret = stringConstructor(cbuf, strlen(cbuf));
            free(cbuf);
        } else {
            UNREACHABLE("Invalid object type (%d)", AS_OBJ(value)->type);
        }
    }
    if (ret) {
        return ret;
    }
    UNREACHABLE("error");
}

const char *typeOfVal(Value val) {
    if (IS_OBJ(val)) {
        return typeOfObj(AS_OBJ(val));
    } else {
        if (IS_BOOL(val)) return "bool";
        if (IS_NIL(val)) return "nil";
        if (IS_NUMBER(val)) return "number";
    }
    UNREACHABLE("Unknown value type! Pointer: %p\n", AS_OBJ(val));
}

uint32_t valHash(Value val) {
    if (IS_OBJ(val)) {
        if (IS_STRING(val)) {
            ObjString *string = AS_STRING(val);
            return hashString(string->chars, string->length);
        } else {
            char buf[20] = {'\0'};
            sprintf(buf, "%p", AS_OBJ(val));
            return hashString(buf, strlen(buf));
        }
    } else if (IS_NUMBER(val)) {
        return ((uint32_t)AS_NUMBER(val))+3;
    } else if (IS_BOOL(val)) { // TODO: return pointer address string hash of singletons
        if (AS_BOOL(val)) {
            return 1;
        } else {
            return 0;
        }
    } else if (IS_NIL(val)) { // TODO: return pointer address string hash of singleton
        return 2;
    } else {
        ASSERT(0);
    }
}

bool valEqual(Value a, Value b) {
    if (a.type != a.type) return false;
    switch (a.type) {
        case VAL_T_BOOL:
            return AS_BOOL(a) == AS_BOOL(b);
        case VAL_T_NIL:
            return true;
        case VAL_T_NUMBER:
            return AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_T_OBJ: {
            Obj *aObj = AS_OBJ(a);
            Obj *bObj = AS_OBJ(b);
            if (aObj->type != bObj->type) return false;
            if (IS_STRING(a)) {
                return strcmp(AS_STRING(a)->chars,
                        AS_STRING(b)->chars) == 0;
            }
            return aObj == bObj; // pointer equality
        }
        case VAL_T_SENTINEL: return false;
        default: UNREACHABLE("");
    }
}

bool is_bool_p(Value val) {
    return IS_BOOL(val);
}
bool is_nil_p(Value val) {
    return IS_NIL(val);
}
bool is_number_p(Value val) {
    return IS_NUMBER(val);
}
bool is_obj_p(Value val) {
    return IS_OBJ(val);
}
