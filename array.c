#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"

ObjClass *lxAryClass;

static void markInternalAry(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    ASSERT(internal);
    ValueArray *valAry = internal->data;
    ASSERT(valAry);
    for (int i = 0; i < valAry->count; i++) {
        Value val = valAry->values[i];
        // XXX: this is needed for GC code not to segfault for some reason,
        // need to investigate. It especially happens after multiple (3) calls
        // to GC.collect().
        if (!IS_OBJ(val)) continue;
        if (AS_OBJ(val)->type <= OBJ_T_INTERNAL) {
            blackenObject(AS_OBJ(val));
        }
    }
}

static void freeInternalAry(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    ASSERT(internal);
    ValueArray *valAry = internal->data;
    ASSERT(valAry);
    freeValueArray(valAry);
    FREE(ValueArray, valAry); // release the actual memory
}

// ex: var a = Array();
//     var b = ["hi", 2, Map()];
Value lxArrayInit(int argCount, Value *args) {
    // TODO: call super?
    CHECK_ARITY("Array#init", 1, -1, argCount);
    Value self = *args;
    DBG_ASSERT(IS_AN_ARRAY(self));
    ObjInstance *selfObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(NULL, 0, markInternalAry, freeInternalAry);
    ValueArray *ary = ALLOCATE(ValueArray, 1);
    initValueArray(ary);
    internalObj->data = ary;
    internalObj->dataSz = sizeof(ValueArray);
    tableSet(&selfObj->hiddenFields, OBJ_VAL(internedString("ary", 3)), OBJ_VAL(internalObj));
    for (int i = 1; i < argCount; i++) {
        writeValueArrayEnd(ary, args[i]);
    }
    ASSERT(ary->count == argCount-1);
    return self;
}

// ex: a.push(1);
static Value lxArrayPush(int argCount, Value *args) {
    CHECK_ARITY("Array#push", 2, 2, argCount);
    Value self = args[0];
    arrayPush(self, args[1]);
    return self;
}

// Deletes last element in array and returns it.
// ex: var a = [1,2,3];
//     print a.pop(); => 3
//     print a; => [1,2]
static Value lxArrayPop(int argCount, Value *args) {
    CHECK_ARITY("Array#pop", 1, 1, argCount);
    return arrayPop(*args);
}

// Adds an element to the beginning of the array and returns `self`
// ex: var a = [1,2,3];
//     a.pushFront(100);
//     print a; => [100, 1, 2, 3];
static Value lxArrayPushFront(int argCount, Value *args) {
    CHECK_ARITY("Array#pushFront", 2, 2, argCount);
    Value self = args[0];
    arrayPushFront(self, args[1]);
    return self;
}

// Deletes an element from the beginning of the array and returns it.
// Returns nil if no elements left.
// ex: var a = [1,2,3];
//     print a.popFront(); => 1
//     print a; => [2,3]
static Value lxArrayPopFront(int argCount, Value *args) {
    CHECK_ARITY("Array#popFront", 1, 1, argCount);
    return arrayPopFront(*args);
}

// ex: a.delete(2);
static Value lxArrayDelete(int argCount, Value *args) {
    CHECK_ARITY("Array#delete", 2, 2, argCount);
    Value self = args[0];
    int idx = arrayDelete(self, args[1]);
    if (idx == -1) {
        return NIL_VAL;
    } else {
        return NUMBER_VAL(idx);
    }
}

// ex: a.clear();
static Value lxArrayClear(int argCount, Value *args) {
    CHECK_ARITY("Array#clear", 1, 1, argCount);
    Value self = args[0];
    arrayClear(self);
    return self;
}

// ex:
//   print a;
// OR
//   a.toString(); // => [1,2,3]
static Value lxArrayToString(int argCount, Value *args) {
    CHECK_ARITY("Array#toString", 1, 1, argCount);
    Value self = *args;
    Obj* selfObj = AS_OBJ(self);
    Value ret = newStringInstance(copyString("[", 1));
    ObjString *bufRet = STRING_GETHIDDEN(ret);
    ValueArray *ary = ARRAY_GETHIDDEN(self);
    for (int i = 0; i < ary->count; i++) {
        Value elVal = ary->values[i];
        if (IS_OBJ(elVal) && (AS_OBJ(elVal) == selfObj)) {
            pushCString(bufRet, "[...]", 5);
            continue;
        }
        if (IS_OBJ(elVal)) {
            DBG_ASSERT(AS_OBJ(elVal)->type > OBJ_T_NONE);
        }
        ObjString *buf = valueToString(elVal, copyString);
        pushCString(bufRet, buf->chars, strlen(buf->chars));
        if (i < (ary->count-1)) {
            pushCString(bufRet, ",", 1);
        }
    }
    pushCString(bufRet, "]", 1);
    return ret;
}


static Value lxArrayOpIndexGet(int argCount, Value *args) {
    CHECK_ARITY("Array#[]", 2, 2, argCount);
    Value self = args[0];
    Value num = args[1];
    CHECK_ARG_BUILTIN_TYPE(num, IS_NUMBER_FUNC, "number", 1);
    ValueArray *ary = ARRAY_GETHIDDEN(self);
    int idx = (int)AS_NUMBER(num);
    if (idx < 0) {
        // FIXME: throw error
        return NIL_VAL;
    }

    if (idx < ary->count) {
        return ary->values[idx];
    } else {
        return NIL_VAL;
    }
}

static Value lxArrayOpIndexSet(int argCount, Value *args) {
    CHECK_ARITY("Array#[]=", 3, 3, argCount);
    Value self = args[0];
    ObjInstance *selfObj = AS_INSTANCE(self);
    Value num = args[1];
    Value rval = args[2];
    CHECK_ARG_BUILTIN_TYPE(num, IS_NUMBER_FUNC, "number", 1);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    Value internalObjVal;
    ASSERT(tableGet(&selfObj->hiddenFields, OBJ_VAL(internedString("ary", 3)), &internalObjVal));
    ValueArray *ary = (ValueArray*)internalGetData(AS_INTERNAL(internalObjVal));
    ASSERT(ary);
    int idx = (int)AS_NUMBER(num);
    if (idx < 0) {
        // FIXME: throw error, or allow negative indices?
        return NIL_VAL;
    }

    if (idx < ary->count) {
        ary->values[idx] = rval;
    } else {
        // TODO: throw error or grow array?
        return NIL_VAL;
    }
    return rval;
}

static Value lxArrayIter(int argCount, Value *args) {
    CHECK_ARITY("Array#iter", 1, 1, argCount);
    return createIterator(*args);
}

static Value lxArrayOpEquals(int argCount, Value *args) {
    CHECK_ARITY("Array#==", 2, 2, argCount);
    return BOOL_VAL(arrayEquals(args[0], args[1]));
}

static Value lxArrayGetSize(int argCount, Value *args) {
    ValueArray *ary = ARRAY_GETHIDDEN(*args);
    return NUMBER_VAL(ary->count);
}

void Init_ArrayClass() {
    // class Array
    ObjClass *arrayClass = addGlobalClass("Array", lxObjClass);
    lxAryClass = arrayClass;

    addNativeMethod(arrayClass, "init", lxArrayInit);
    // methods
    addNativeMethod(arrayClass, "push", lxArrayPush);
    addNativeMethod(arrayClass, "pop", lxArrayPop);
    addNativeMethod(arrayClass, "pushFront", lxArrayPushFront);
    addNativeMethod(arrayClass, "popFront", lxArrayPopFront);
    addNativeMethod(arrayClass, "delete", lxArrayDelete);
    addNativeMethod(arrayClass, "opIndexGet", lxArrayOpIndexGet);
    addNativeMethod(arrayClass, "opIndexSet", lxArrayOpIndexSet);
    addNativeMethod(arrayClass, "opEquals", lxArrayOpEquals);
    addNativeMethod(arrayClass, "toString", lxArrayToString);
    addNativeMethod(arrayClass, "iter", lxArrayIter);
    addNativeMethod(arrayClass, "clear", lxArrayClear);

    // getters
    addNativeGetter(arrayClass, "size", lxArrayGetSize);
}
