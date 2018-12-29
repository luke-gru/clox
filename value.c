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

void writeValueArrayEnd(ValueArray *array, Value value) {
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

void writeValueArrayBeg(ValueArray *array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(
            array->values, Value,
            oldCapacity, array->capacity
        );
    }

    Value *dest = array->values+1;
    Value *src = array->values;
    memmove(dest, src, sizeof(Value));
    array->values[0] = value;
    array->count++;
}

void freeValueArray(ValueArray *array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    array->values = NULL;
    initValueArray(array);
}

/**
 *  0  *1*  2   3
 * [1,  2,  3,  4]
 * [1,  3]
 *
 */
// NOTE: assumes index is within bounds
bool removeValueArray(ValueArray *array, int idx) {
    ASSERT(idx < array->count);
    if (array->values == NULL) return false;
    if (idx == array->count-1) { // last element
        array->count--;
        return true;
    }
    Value *dest = array->values + idx;
    Value *src = dest + 1;
    size_t szNum = array->count - idx - 1;
    memmove(dest, src, sizeof(Value) * szNum);
    array->count--;
    return true;
}

static int printBool(FILE *file, bool val, int maxLen) {
    return fprintf(file, "%.*s", maxLen, val ? "true" : "false");
}

static int printNumber(FILE *file, double number, int maxLen) {
    return fprintf(file, "%g", number);
}

int printValue(FILE *file, Value value, bool canCallMethods, int maxLen) {
#define PRINTNUM(a) ((a))
    if (IS_BOOL(value)) {
        return printBool(file, AS_BOOL(value), maxLen);
    } else if (IS_NIL(value)) {
        return fprintf(file, "%.*s", PRINTNUM(maxLen), "nil");
    } else if (IS_NUMBER(value)) {
        return printNumber(file, AS_NUMBER(value), maxLen);
    } else if (IS_OBJ(value)) {
        if (IS_STRING(value)) {
            char *cstring = AS_CSTRING(value);
            return fprintf(file, "%.*s", PRINTNUM(maxLen), cstring ? cstring : "(NULL)");
        } else if (IS_FUNCTION(value) || IS_CLOSURE(value)) {
            ObjFunction *func = NULL;
            if (IS_FUNCTION(value)) {
                func = AS_FUNCTION(value);
            } else {
                func = AS_CLOSURE(value)->function;
            }
            if (func->name == NULL) {
                return fprintf(file, "%.*s", PRINTNUM(maxLen), "<fun (Anon)>");
            } else {
                return fprintf(file, "<fun %.*s>", PRINTNUM(maxLen-6), func->name->chars);
            }
        } else if (IS_INSTANCE(value)) {
            ObjInstance *inst = AS_INSTANCE(value);
            Obj *callable = instanceFindMethod(inst, internedString("toString", 8));
            if (callable && vm.inited && canCallMethods) {
                Value stringVal = callVMMethod(inst, OBJ_VAL(callable), 0, NULL);
                if (!IS_A_STRING(stringVal)) {
                    throwErrorFmt(lxTypeErrClass, "TypeError, toString() returned non-string, is a: %s", typeOfVal(stringVal));
                    UNREACHABLE_RETURN(-1);
                }
                ObjString *out = VAL_TO_STRING(stringVal);
                int ret = fprintf(file, "%.*s", maxLen, out->chars);
                Value popped = pop();
                ASSERT(AS_OBJ(popped) == AS_OBJ(stringVal));
                return ret;
            } else {
                if (IS_A_STRING(value)) { // when canCallMethods == false
                    ObjString *str = VAL_TO_STRING(value);
                    if (str) {
                        return fprintf(file, "\"%.*s\"", PRINTNUM(maxLen-2), str->chars);
                    } else {
                        // Shouldn't happen, but happens sometimes when debugging the GC
                        return fprintf(file, "??unknown string??");
                    }
                } else {
                    ObjClass *klass = inst->klass;
                    char *klassName = CLASSINFO(klass)->name->chars;
                    return fprintf(file, "<instance %.*s>", PRINTNUM(maxLen-11), klassName);
                }
            }
        } else if (OBJ_TYPE(value) == OBJ_T_CLASS) {
            ObjClass *klass = AS_CLASS(value);
            char *klassName = CLASSINFO(klass)->name ? CLASSINFO(klass)->name->chars : "(anon)";
            return fprintf(file, "<class %.*s>", PRINTNUM(maxLen-8), klassName);
        } else if (OBJ_TYPE(value) == OBJ_T_MODULE) {
            ObjModule *mod = AS_MODULE(value);
            char *modName = CLASSINFO(mod)->name ? CLASSINFO(mod)->name->chars : "(anon)";
            return fprintf(file, "<module %.*s>", PRINTNUM(maxLen-9), modName);
        } else if (OBJ_TYPE(value) == OBJ_T_NATIVE_FUNCTION) {
            ObjNative *native = AS_NATIVE_FUNCTION(value);
            ObjString *name = native->name;
            return fprintf(file, "<fn %.*s (native)>", PRINTNUM(maxLen-14), name->chars);
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
            return fprintf(file, "<method %.*s>", PRINTNUM(maxLen-9), name->chars);
        } else if (OBJ_TYPE(value) == OBJ_T_INTERNAL) {
            return fprintf(file, "%.*s", PRINTNUM(maxLen-10), "<internal>");
        } else {
            UNREACHABLE("Unknown object type: valtype=%s (objtype=%d)",
                typeOfVal(value),
                AS_OBJ(value)->type
            );
        }
    }
    fprintf(file, "Unknown value type: %d. Cannot print!\n", value.type);
    UNREACHABLE("BUG");
    return -1;
#undef PRINTNUM
}

// returns a new ObjString
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
        snprintf(buftemp, 50, "%g", d); // ex: "1.2"
        char *buf = calloc(strlen(buftemp)+1, 1);
        ASSERT_MEM(buf);
        strcpy(buf, buftemp);
        ret = stringConstructor(buf, strlen(buf));
        xfree(buf);
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
                xfree(buf);
            }
        } else if (OBJ_TYPE(value) == OBJ_T_INSTANCE) {
            ObjInstance *inst = AS_INSTANCE(value);
            Obj *toString = instanceFindMethod(inst, internedString("toString", 8));
            if (toString && vm.inited) {
                Value stringVal = callVMMethod(inst, OBJ_VAL(toString), 0, NULL);
                if (!IS_A_STRING(stringVal)) {
                    throwErrorFmt(lxTypeErrClass, "TypeError, toString() returned non-string, is a: %s", typeOfVal(stringVal));
                    UNREACHABLE_RETURN(NULL);
                }
                char *cbuf = VAL_TO_STRING(stringVal)->chars;
                ret = stringConstructor(cbuf, strlen(cbuf));
                pop(); // stringVal
            } else {
                ObjClass *klass = inst->klass;
                char *klassName = CLASSINFO(klass)->name->chars;
                char *cbuf = calloc(strlen(klassName)+1+11, 1);
                ASSERT_MEM(cbuf);
                sprintf(cbuf, "<instance %s>", klassName);
                ret = stringConstructor(cbuf, strlen(cbuf));
                xfree(cbuf);
            }
        } else if (OBJ_TYPE(value) == OBJ_T_CLASS) {
            ObjClass *klass = AS_CLASS(value);
            char *klassName = CLASSINFO(klass)->name->chars;
            char *cbuf = calloc(strlen(klassName)+1+8, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<class %s>", klassName);
            ret = stringConstructor(cbuf, strlen(cbuf));
            xfree(cbuf);
        } else if (OBJ_TYPE(value) == OBJ_T_MODULE) {
            ObjModule *mod = AS_MODULE(value);
            char *modName = CLASSINFO(mod)->name->chars;
            char *cbuf = calloc(strlen(modName)+1+9, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<module %s>", modName);
            ret = stringConstructor(cbuf, strlen(cbuf));
            xfree(cbuf);
        } else if (OBJ_TYPE(value) == OBJ_T_NATIVE_FUNCTION) {
            ObjNative *native = AS_NATIVE_FUNCTION(value);
            ObjString *name = native->name;
            char *nameStr = name->chars;
            char *cbuf = calloc(strlen(nameStr)+1+14, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<fn %s (native)>", nameStr);
            ret = stringConstructor(cbuf, strlen(cbuf));
            xfree(cbuf);
        } else if (OBJ_TYPE(value) == OBJ_T_BOUND_METHOD) {
            ObjBoundMethod *bmethod = AS_BOUND_METHOD(value);
            ObjString *name;
            if (bmethod->callable->type == OBJ_T_CLOSURE) {
                name = ((ObjClosure*)(bmethod->callable))->function->name;
            } else if (bmethod->callable->type == OBJ_T_NATIVE_FUNCTION) {
                name = ((ObjNative*)(bmethod->callable))->name;
            } else {
                fprintf(stderr, "Wrong obj type: %d\n", bmethod->callable->type);
                UNREACHABLE("error");
            }
            char *nameStr = name->chars;
            char *cbuf = calloc(strlen(nameStr)+1+9, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<method %s>", nameStr);
            ret = stringConstructor(cbuf, strlen(cbuf));
            xfree(cbuf);
        } else {
            UNREACHABLE("Invalid object type (%d)", AS_OBJ(value)->type);
        }
    }
    if (ret) {
        return ret;
    }
    UNREACHABLE("error: invalid type given %s", typeOfVal(value));
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
        if (IS_STRING(val) || IS_A_STRING(val)) {
            ObjString *string = VAL_TO_STRING(val);
            if (string->hash > 0) {
                return string->hash;
            } else {
                uint32_t hash = hashString(string->chars, string->length);
                string->hash = hash;
                return hash;
            }
        } else {
            if (IS_INSTANCE(val)) {
                Value hashKey = callMethod(AS_OBJ(val), internedString("hashKey", 7), 0, NULL);
                if (!IS_NUMBER(hashKey)) {
                    throwErrorFmt(lxTypeErrClass, "%s", "return of hashKey() method must be a number!");
                }
                return (uint32_t)AS_NUMBER(hashKey);
            }
            char buf[20] = {'\0'};
            sprintf(buf, "%p", AS_OBJ(val));
            return hashString(buf, strlen(buf)); // hash the pointer string
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
    switch (a.type) {
        case VAL_T_BOOL:
            return b.type == VAL_T_BOOL && AS_BOOL(a) == AS_BOOL(b);
        case VAL_T_NIL:
            return b.type == VAL_T_NIL;
        case VAL_T_NUMBER:
            return b.type == VAL_T_NUMBER && AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_T_OBJ: {
            // internal string equality (ObjString)
            if (IS_STRING(a) && IS_STRING(b)) {
                return strcmp(VAL_TO_STRING(a)->chars,
                        VAL_TO_STRING(b)->chars) == 0;
            }
            if (IS_INSTANCE_LIKE(a)) { // including lox strings
                return AS_BOOL(callMethod(AS_OBJ(a), internedString("opEquals", 8), 1, &b));
            }
            UNREACHABLE_RETURN(false);
        }
        case VAL_T_UNDEF: return false;
        default: UNREACHABLE("");
    }
}

bool isCallable(Value val) {
    return IS_CLASS(val) || IS_NATIVE_FUNCTION(val) ||
        IS_BOUND_METHOD(val) || IS_CLOSURE(val);
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
