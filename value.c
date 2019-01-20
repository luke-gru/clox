#include <stdio.h>
#include <inttypes.h>

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

void initValueArrayWithCapa(ValueArray *array, int capa) {
    initValueArray(array);
    if (capa <= 0) return;
    array->capacity = capa;
    array->values = GROW_ARRAY(
        array->values, Value,
        0, array->capacity
    );
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

void writeValueArrayBulk(ValueArray *ary, size_t offset, size_t num, Value fill) {
    ASSERT(ary->values);
    for (size_t i = 0; i < num; i++) {
        memcpy(ary->values+offset+i, &fill, sizeof(Value));
    }
    ary->count+=num;
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

#define PRINTNUM(other, max) ((other == 0 ? max : (max == -1 ? -1 : (max > other ? max-other : 0))))
static int printBool(FILE *file, bool val, int maxLen) {
    return fprintf(file, "%.*s", PRINTNUM(0, maxLen), val ? "true" : "false");
}

static int printNumber(FILE *file, double number, int maxLen) {
    // TODO: use maxLen
    return fprintf(file, "%g", number);
}

int printValue(FILE *file, Value value, bool canCallMethods, int maxLen) {
    if (IS_BOOL(value)) {
        return printBool(file, AS_BOOL(value), maxLen);
    } else if (IS_NIL(value)) {
        return fprintf(file, "%.*s", PRINTNUM(0, maxLen), "nil");
    } else if (IS_NUMBER(value)) {
        return printNumber(file, AS_NUMBER(value), maxLen);
    } else if (IS_UNDEF(value)) {
        return fprintf(file, "%.*s", PRINTNUM(0,maxLen), "undef");
    } else if (IS_OBJ(value)) {
        if (IS_STRING(value)) {
            char *cstring = AS_CSTRING(value);
            return fprintf(file, "%.*s", PRINTNUM(0, maxLen), cstring ? cstring : "(NULL)");
        } else if (IS_FUNCTION(value) || IS_CLOSURE(value)) {
            ObjFunction *func = NULL;
            if (IS_FUNCTION(value)) {
                func = AS_FUNCTION(value);
            } else {
                func = AS_CLOSURE(value)->function;
            }
            if (func->name == NULL) {
                return fprintf(file, "%.*s", PRINTNUM(0, maxLen), "<fun (Anon)>");
            } else {
                return fprintf(file, "<fun %.*s>", PRINTNUM(6, maxLen), func->name->chars);
            }
        } else if (IS_INSTANCE_LIKE(value)) {
            ObjInstance *inst = AS_INSTANCE(value);
            Obj *callable = instanceFindMethod(inst, INTERNED("toString", 8));
            if (callable && vm.inited && canCallMethods) {
                Value stringVal = callVMMethod(inst, OBJ_VAL(callable), 0, NULL, NULL);
                if (!IS_A_STRING(stringVal)) {
                    pop();
                    throwErrorFmt(lxTypeErrClass, "TypeError, toString() returned non-string, is a: %s", typeOfVal(stringVal));
                    UNREACHABLE_RETURN(-1);
                }
                ObjString *out = VAL_TO_STRING(stringVal);
                int ret = fprintf(file, "%.*s", maxLen, out->chars);
                Value popped = pop();
                ASSERT(AS_OBJ(popped) == AS_OBJ(stringVal));
                return ret;
            } else {
                if (IS_STRING(value)) { // when canCallMethods == false
                    ObjString *str = VAL_TO_STRING(value);
                    if (str) {
                        return fprintf(file, "\"%.*s\"", PRINTNUM(2, maxLen), str->chars);
                    } else {
                        // Shouldn't happen, but happens sometimes when debugging the GC
                        return fprintf(file, "??unknown string??");
                    }
                } else if (IS_ARRAY(value)) {
                    fprintf(file, "[");
                    ValueArray *ary = &AS_ARRAY(value)->valAry;
                    Value el; int elIdx = 0;
                    // XXX: this can overflow stack if array contains itself...
                    VALARRAY_FOREACH(ary, el, elIdx) {
                        printValue(file, el, canCallMethods, maxLen);
                        fprintf(file, ",");
                    }
                    return fprintf(file, "]");
                } else {
                    ObjClass *klass = inst->klass;
                    char *klassName = CLASSINFO(klass)->name->chars;
                    return fprintf(file, "<instance %.*s>", PRINTNUM(11, maxLen), klassName);
                }
            }
        } else if (OBJ_TYPE(value) == OBJ_T_CLASS) {
            ObjClass *klass = AS_CLASS(value);
            char *klassName = CLASSINFO(klass)->name ? CLASSINFO(klass)->name->chars : (char*)"(anon)";
            return fprintf(file, "<class %.*s>", PRINTNUM(8, maxLen), klassName);
        } else if (OBJ_TYPE(value) == OBJ_T_MODULE) {
            ObjModule *mod = AS_MODULE(value);
            char *modName = CLASSINFO(mod)->name ? CLASSINFO(mod)->name->chars : (char*)"(anon)";
            return fprintf(file, "<module %.*s>", PRINTNUM(9, maxLen), modName);
        } else if (OBJ_TYPE(value) == OBJ_T_NATIVE_FUNCTION) {
            ObjNative *native = AS_NATIVE_FUNCTION(value);
            ObjString *name = native->name;
            return fprintf(file, "<fn %.*s (native)>", PRINTNUM(14, maxLen), name->chars);
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
            return fprintf(file, "<method %.*s>", PRINTNUM(9, maxLen), name->chars);
        } else if (OBJ_TYPE(value) == OBJ_T_INTERNAL) {
            return fprintf(file, "%.*s", PRINTNUM(10, maxLen), "<internal>");
        } else {
            UNREACHABLE("Unknown object type: valtype=%s (objtype=%d)",
                typeOfVal(value),
                AS_OBJ(value)->type
            );
        }
    }
#ifdef NAN_TAGGING
    fprintf(file, "Unknown value: %" PRIu64 ". Cannot print!\n", value);
#else
    fprintf(file, "Unknown value type: %d. Cannot print!\n", value.type);
#endif
    UNREACHABLE("BUG");
    return -1;
#undef PRINTNUM
}

// returns a new ObjString
ObjString *valueToString(Value value, newStringFunc stringConstructor, int flags) {
    ASSERT(stringConstructor != takeString); // should copy the constructed c string
    ObjString *ret = NULL;
    if (IS_BOOL(value)) {
        if (AS_BOOL(value)) {
            ret = stringConstructor("true", 4, flags);
        } else {
            ret = stringConstructor("false", 5, flags);
        }
    } else if (IS_NIL(value)) {
        ret = stringConstructor("nil", 3, flags);
    } else if (IS_NUMBER(value)) {
        char buftemp[50] = { '\0' };
        double d = AS_NUMBER(value);
        snprintf(buftemp, 50, "%g", d); // ex: "1.2"
        char *buf = calloc(strlen(buftemp)+1, 1);
        ASSERT_MEM(buf);
        strcpy(buf, buftemp);
        ret = stringConstructor(buf, strlen(buf), flags);
        xfree(buf);
    } else if (IS_OBJ(value)) {
        if (OBJ_TYPE(value) == OBJ_T_STRING) {
            char *cstring = AS_CSTRING(value);
            ASSERT(cstring);
            ret = stringConstructor(cstring, strlen(cstring), flags);
        } else if (IS_FUNCTION(value) || IS_CLOSURE(value)) {
            ObjFunction *func = NULL;
            if (IS_FUNCTION(value)) {
                func = AS_FUNCTION(value);
            } else {
                func = AS_CLOSURE(value)->function;
            }
            if (func->name == NULL) {
                const char *anon = "<fun (Anon)>";
                ret = stringConstructor((char*)anon, strlen(anon), flags);
            } else {
                char *buf = calloc(strlen(func->name->chars)+1+6, 1);
                ASSERT_MEM(buf);
                sprintf(buf, "<fun %s>", func->name->chars);
                ret = stringConstructor(buf, strlen(buf), flags);
                xfree(buf);
            }
        } else if (OBJ_TYPE(value) == OBJ_T_INSTANCE || OBJ_TYPE(value) == OBJ_T_ARRAY || OBJ_TYPE(value) == OBJ_T_STRING || OBJ_TYPE(value) == OBJ_T_MAP || OBJ_TYPE(value) == OBJ_T_REGEX) {
            ObjInstance *inst = AS_INSTANCE(value);
            Obj *toString = instanceFindMethod(inst, INTERN("toString"));
            if (toString && vm.inited) {
                Value stringVal = callVMMethod(inst, OBJ_VAL(toString), 0, NULL, NULL);
                if (!IS_A_STRING(stringVal)) {
                    pop();
                    throwErrorFmt(lxTypeErrClass, "TypeError, toString() returned non-string, is a: %s", typeOfVal(stringVal));
                    UNREACHABLE_RETURN(NULL);
                }
                char *cbuf = VAL_TO_STRING(stringVal)->chars;
                ret = stringConstructor(cbuf, strlen(cbuf), flags);
                pop(); // stringVal
            } else {
                ObjClass *klass = inst->klass;
                char *klassName = CLASSINFO(klass)->name->chars;
                char *cbuf = calloc(strlen(klassName)+1+11, 1);
                ASSERT_MEM(cbuf);
                sprintf(cbuf, "<instance %s>", klassName);
                ret = stringConstructor(cbuf, strlen(cbuf), flags);
                xfree(cbuf);
            }
        } else if (OBJ_TYPE(value) == OBJ_T_CLASS) {
            ObjClass *klass = AS_CLASS(value);
            char *klassName = CLASSINFO(klass)->name->chars;
            char *cbuf = calloc(strlen(klassName)+1+8, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<class %s>", klassName);
            ret = stringConstructor(cbuf, strlen(cbuf), flags);
            xfree(cbuf);
        } else if (OBJ_TYPE(value) == OBJ_T_MODULE) {
            ObjModule *mod = AS_MODULE(value);
            char *modName = CLASSINFO(mod)->name->chars;
            char *cbuf = calloc(strlen(modName)+1+9, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<module %s>", modName);
            ret = stringConstructor(cbuf, strlen(cbuf), flags);
            xfree(cbuf);
        } else if (OBJ_TYPE(value) == OBJ_T_NATIVE_FUNCTION) {
            ObjNative *native = AS_NATIVE_FUNCTION(value);
            ObjString *name = native->name;
            char *nameStr = name->chars;
            char *cbuf = calloc(strlen(nameStr)+1+14, 1);
            ASSERT_MEM(cbuf);
            sprintf(cbuf, "<fn %s (native)>", nameStr);
            ret = stringConstructor(cbuf, strlen(cbuf), flags);
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
            ret = stringConstructor(cbuf, strlen(cbuf), flags);
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
        if (IS_UNDEF(val)) return "UNDEF?"; // for debugging
    }
    UNREACHABLE("Unknown value type! Pointer: %p\n", AS_OBJ(val));
}

// Taken from wren lang
static inline uint32_t hashBits(DoubleBits bits) {
    uint32_t result = bits.bits32[0] ^ bits.bits32[1];

    // Slosh the bits around some. Due to the way doubles are represented, small
    // integers will have most of low bits of the double respresentation set to
    // zero. For example, the above result for 5 is 43d00600.
    //
    // We map that to an entry index by masking off the high bits which means
    // most small integers would all end up in entry zero. That's bad. To avoid
    // that, push a bunch of the high bits lower down so they affect the lower
    // bits too.
    //
    // The specific mixing function here was pulled from Java's HashMap
    // implementation.
    result ^= (result >> 20) ^ (result >> 12);
    result ^= (result >> 7) ^ (result >> 4);
    return result;
}

uint32_t valHash(Value val) {
    if (IS_OBJ(val)) {
        if (LIKELY(IS_STRING(val))) {
            ObjString *string = AS_STRING(val);
            if (LIKELY(string->hash > 0)) {
                return string->hash;
            } else {
                uint32_t hash = hashString(string->chars, string->length);
                string->hash = hash;
                return hash;
            }
        } else {
            if (IS_INSTANCE(val) || IS_ARRAY(val) || IS_MAP(val)) {
                if (UNLIKELY(!vm.inited)) {
                    fprintf(stderr, "val type: %s\n", typeOfVal(val));
                    ASSERT(0);
                }
                Value hashKey = callMethod(AS_OBJ(val), INTERNED("hashKey", 7), 0, NULL, NULL);
                if (UNLIKELY(!IS_NUMBER(hashKey))) {
                    throwErrorFmt(lxTypeErrClass, "%s", "return of hashKey() method must be a number!");
                }
                return (uint32_t)AS_NUMBER(hashKey);
            }
// TODO: use the hashBits function, and change Map to use an ordered table.
// Right now the table in table.c does not preserve insertion order, but it
// turns out it works when we hash the pointer repr of the object (at least,
// depending on the malloc() implementation and the OS).
#if 0
            // Hash the raw bits of the unboxed value.
            DoubleBits bits;
            bits.bits64 = val;
            return hashBits(bits);
#else
            // XXX: hack
            char buf[20] = {'\0'};
            snprintf(buf, 20, "%p", AS_OBJ(val));
            return hashString(buf, strlen(buf)); // hash the pointer string, easiest way but brittle, because an object is not equal to its string pointer repr...
#endif
        }
    } else {
#if 0
        // Hash the raw bits of the unboxed value.
        DoubleBits bits;
        bits.bits64 = val;
        return hashBits(bits);
#else
        if (LIKELY(IS_NUMBER(val))) {
            return ((uint32_t)AS_NUMBER(val))+3;
        } else if (IS_BOOL(val)) {
            if (AS_BOOL(val)) {
                return 1;
            } else {
                return 0;
            }
        } else if (IS_NIL(val)) {
            return 2;
        } else {
            ASSERT(0);
        }
#endif
    }
}

// a == b
bool valEqual(Value a, Value b) {
#ifdef NAN_TAGGING
    switch (a) {
        case TRUE_VAL:
            return b == TRUE_VAL;
        case FALSE_VAL:
            return b == FALSE_VAL;
        case NIL_VAL:
            return b == NIL_VAL;
        case UNDEF_VAL:
            return false;
        default: {
            if (IS_STRING(a) && IS_STRING(b)) {
                ObjString *aStr = AS_STRING(a);
                ObjString *bStr = AS_STRING(b);
                if (aStr->hash && bStr->hash) {
                    return aStr->hash == bStr->hash;
                }
                return aStr->length == bStr->length && strcmp(aStr->chars, bStr->chars) == 0;
            }
            if (IS_INSTANCE_LIKE(a)) {
                return AS_BOOL(callMethod(AS_OBJ(a), INTERNED("opEquals", 8), 1, &b, NULL));
            }
            return a == b;
        }
    }
#else
    switch (a.type) {
        case VAL_T_TRUE:
            return b.type == VAL_T_TRUE;
        case VAL_T_FALSE:
            return b.type == VAL_T_FALSE;
        case VAL_T_NIL:
            return b.type == VAL_T_NIL;
        case VAL_T_NUMBER:
            return b.type == VAL_T_NUMBER && AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_T_OBJ: {
            if (IS_STRING(a) && IS_STRING(b)) {
                return strcmp(VAL_TO_STRING(a)->chars,
                        VAL_TO_STRING(b)->chars) == 0;
            }
            if (IS_INSTANCE_LIKE(a)) {
                return AS_BOOL(callMethod(AS_OBJ(a), INTERNED("opEquals", 8), 1, &b, NULL));
            }
            UNREACHABLE_RETURN(false);
        }
        case VAL_T_UNDEF: return false;
        default: UNREACHABLE("valEqual");
    }
#endif
}

bool isTruthy(Value val) {
#ifdef NAN_TAGGING
    switch (val) {
    case NIL_VAL: return false;
    case TRUE_VAL: return true;
    case FALSE_VAL: return false;
    case UNDEF_VAL: UNREACHABLE("undefined value found?");
    default:
        // all other values are truthy
        return true;

    }
#else
    switch (val.type) {
    case VAL_T_NIL: return false;
    case VAL_T_TRUE:
    case VAL_T_FALSE:
        return AS_BOOL(val);
    case VAL_T_UNDEF: UNREACHABLE("undefined value found?");
    default:
        // all other values are truthy
        return true;

    }
#endif
}

void fillCallableName(Value callable, char *buf, size_t buflen) {
    memset(buf, 0, buflen);
    ASSERT(isCallable(callable));
    if (IS_CLASS(callable)) {
        snprintf(buf, buflen, "%s#init", className(AS_CLASS(callable)));
    } else if (IS_NATIVE_FUNCTION(callable)) {
        ObjNative *native = AS_NATIVE_FUNCTION(callable);
        char *nameStr = native->name->chars;
        if (native->klass) { // method
            char *classNm = className((ObjClass*)native->klass);
            bool isStatic = native->isStatic;
            snprintf(buf, buflen, "%s%c%s", classNm, isStatic ? '.' : '#',
                    nameStr);
        } else {
            snprintf(buf, buflen, "%s", nameStr);
        }
    } else if (IS_CLOSURE(callable)) {
        ObjFunction *func = AS_CLOSURE(callable)->function;
        if (func->klass) {
            ObjClass *klass = (ObjClass*)func->klass;
            char *classNm = className(klass);
            bool isStatic = func->isSingletonMethod;
            snprintf(buf, buflen, "%s%c%s", classNm, isStatic ? '.' : '#',
                    func->name->chars);
        } else {
            snprintf(buf, buflen, "%s", func->name ? func->name->chars : "(anon)");
        }
    } else { // bound method
        snprintf(buf, buflen, "%s", "TODO"); // TODO
    }
}

ObjString *getCallableFunctionName(Value callable) {
    ASSERT(isCallable(callable));
    if (IS_CLASS(callable)) {
        return vm.initString;
    } else if (IS_NATIVE_FUNCTION(callable)) {
        ObjNative *native = AS_NATIVE_FUNCTION(callable);
        return native->name;
    } else if (IS_CLOSURE(callable)) {
        ObjFunction *func = AS_CLOSURE(callable)->function;
        return func->name;
    } else if (IS_BOUND_METHOD(callable)) {
        return getCallableFunctionName(OBJ_VAL(AS_BOUND_METHOD(callable)->callable));
    } else {
        UNREACHABLE_RETURN(NULL);
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
