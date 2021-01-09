#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"
#include "compiler.h"

ObjClass *lxAryClass;
ObjNative *nativeArrayInit = NULL;

// ex: var a = Array();
//     var b = ["hi", 2, Map()];
static Value lxArrayInit(int argCount, Value *args) {
    CHECK_ARITY("Array#init", 1, -1, argCount);
    callSuper(0, NULL, NULL);
    Value self = *args;
    DBG_ASSERT(IS_AN_ARRAY(self));
    ObjArray *selfObj = AS_ARRAY(self);
    ValueArray *ary = &selfObj->valAry;
    int capa = argCount-1;
    if (capa > 1) {
        initValueArrayWithCapa(ary, capa);
    } else {
        initValueArray(ary);
    }
    for (int i = 1; i < argCount; i++) {
        writeValueArrayEnd(ary, args[i]);
        OBJ_WRITE(self, args[i]);
    }
    DBG_ASSERT(ary->count == argCount-1);
    return self;
}

static Value lxArrayDup(int argCount, Value *args) {
    CHECK_ARITY("Array#dup", 1, 1, argCount);
    Value self = *args;
    Value dup = callSuper(0, NULL, NULL);
    ObjArray *selfObj = AS_ARRAY(self);
    ObjArray *dupObj = AS_ARRAY(dup);
    ValueArray *selfAry = &selfObj->valAry;
    ValueArray *dupAry = &dupObj->valAry;
    // XXX: might be slow to dup large arrays, should bulk copy memory using memcpy or similar
    Value el; int idx = 0;
    VALARRAY_FOREACH(selfAry, el, idx) {
        writeValueArrayEnd(dupAry, el);
        OBJ_WRITE(dup, el);
    }
    return dup;
}

static Value lxArrayInspect(int argCount, Value *args) {
    CHECK_ARITY("Array#inspect", 1, 1, argCount);
    Value self = *args;
    ObjArray *selfObj = AS_ARRAY(self);
    ValueArray *selfAry = &selfObj->valAry;
    ObjString *buf = emptyString();
    pushCString(buf, "[", 1);
    Value el; int idx = 0;
    ObjString *res;
    VALARRAY_FOREACH(selfAry, el, idx) {
      res = inspectString(el);
      pushCString(buf, res->chars, strlen(res->chars));
      if (idx != selfAry->count-1) {
        pushCString(buf, ",", 1);
      }
    }
    pushCString(buf, "]", 1);
    return OBJ_VAL(buf);
}

static Value lxArrayFirst(int argCount, Value *args) {
    CHECK_ARITY("Array#first", 1, 1, argCount);
    Value self = args[0];
    return arrayFirst(self);
}

static Value lxArrayLast(int argCount, Value *args) {
    CHECK_ARITY("Array#last", 1, 1, argCount);
    Value self = args[0];
    return arrayLast(self);
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

static Value lxArrayDeleteAt(int argCount, Value *args) {
    CHECK_ARITY("Array#deleteAt", 2, 2, argCount);
    Value self = args[0];
    Value num = args[1];
    CHECK_ARG_BUILTIN_TYPE(num, IS_NUMBER_FUNC, "number", 1);
    Value found;
    bool deleted = arrayDeleteAt(self, (int)AS_NUMBER(num), &found);
    if (deleted) {
        return found;
    } else {
        return NIL_VAL;
    }
}

// ex: a.clear();
static Value lxArrayClear(int argCount, Value *args) {
    CHECK_ARITY("Array#clear", 1, 1, argCount);
    Value self = args[0];
    arrayClear(self);
    return self;
}

static Value lxArrayJoin(int argCount, Value *args) {
    CHECK_ARITY("Array#join", 2, 2, argCount);
    Value self = args[0];
    ValueArray *valAry = &AS_ARRAY(self)->valAry;
    Value joinVal = args[1];
    CHECK_ARG_IS_A(joinVal, lxStringClass, 1);
    ObjString *joinStr = AS_STRING(joinVal);
    size_t joinStrLen = strlen(joinStr->chars);
    ObjString *buf = copyString("", 0, NEWOBJ_FLAG_NONE);
    Value el; int elIdx = 0;
    int count = valAry->count;
    VALARRAY_FOREACH(valAry, el, elIdx) {
        ObjString *elStr = NULL;
        if (IS_A_STRING(el)) {
            elStr = AS_STRING(el);
        } else {
            elStr = valueToString(el, copyString, NEWOBJ_FLAG_NONE);
        }
        pushCString(buf, (const char*)elStr->chars, strlen(elStr->chars));
        if (elIdx+1 < count) {
            pushCString(buf, (const char*)joinStr->chars, joinStrLen);
        }
    }
    return OBJ_VAL(buf);
}

// returns a newly sorted array. Each element must be comparable (number or
// string)
static Value lxArraySort(int argCount, Value *args) {
    CHECK_ARITY("Array#sort", 1, 1, argCount);
    Value self = *args;
    return arraySort(self);
}

static Value lxArraySortBy(int argCount, Value *args) {
    CHECK_ARITY("Array#sortBy", 1, 1, argCount);
    Value self = *args;
    if (!blockGiven()) {
        throwErrorFmt(lxArgErrClass, "Block must be given");
    }
    return arraySortBy(self);
}

// ex:
//   print a;
// OR
//   a.toString(); // => [1,2,3]
static Value lxArrayToString(int argCount, Value *args) {
    CHECK_ARITY("Array#toString", 1, 1, argCount);
    Value self = *args;
    Obj *selfObj = AS_OBJ(self);
    Value ret = OBJ_VAL(copyString("[", 1, NEWOBJ_FLAG_NONE));
    ObjString *bufRet = AS_STRING(ret);
    ObjArray *aryObj = AS_ARRAY(self);
    ValueArray *ary = &aryObj->valAry;
    for (int i = 0; i < ary->count; i++) {
        Value elVal = ary->values[i];
        if (IS_OBJ(elVal) && (AS_OBJ(elVal) == selfObj)) {
            pushCString(bufRet, "[...]", 5);
            continue;
        }
        if (IS_OBJ(elVal)) {
            DBG_ASSERT(AS_OBJ(elVal)->type > OBJ_T_NONE);
        }
        ObjString *buf = valueToString(elVal, copyString, NEWOBJ_FLAG_NONE);
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
    ValueArray *ary = &AS_ARRAY(self)->valAry;
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
    ObjArray *selfObj = AS_ARRAY(self);
    Value num = args[1];
    Value rval = args[2];
    CHECK_ARG_BUILTIN_TYPE(num, IS_NUMBER_FUNC, "number", 1);
    if (isFrozen((Obj*)selfObj)) {
        throwErrorFmt(lxErrClass, "%s", "Array is frozen, cannot modify");
    }
    ValueArray *ary = &selfObj->valAry;
    int idx = (int)AS_NUMBER(num);
    if (idx < 0) {
        // FIXME: throw error, or allow negative indices?
        return NIL_VAL;
    }

    if (idx < ary->count) {
        ary->values[idx] = rval;
        OBJ_WRITE(self, rval);
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

// FIXME: figure out how to hash this properly
static Value lxArrayHashKey(int argCount, Value *args) {
    CHECK_ARITY("Array#hashKey", 1, 1, argCount);
    Value self = *args;
    uint32_t hash = 16679; // XXX: no reason for this number
    ValueArray *ary = &AS_ARRAY(self)->valAry;
    Value el; int idx = 0;
    VALARRAY_FOREACH(ary, el, idx) {
        if (AS_OBJ(el) == AS_OBJ(self)) { // avoid infinite recursion
            hash = hash ^ 1667; // XXX: no reason for this number
            continue;
        }
        hash = hash ^ valHash(el);
    }
    return NUMBER_VAL(hash);
}

static Value lxArrayFillStatic(int argCount, Value *args) {
    CHECK_ARITY("Array.fill", 2, 3, argCount);
    Value capa = args[1];
    Value fill = NIL_VAL;
    CHECK_ARG_BUILTIN_TYPE(capa, IS_NUMBER_FUNC, "number", 1);
    if (argCount == 3) {
        fill = args[2];
    }
    int capaNum = (int)AS_NUMBER(capa);
    Value ret = OBJ_VAL(newInstance(lxAryClass, NEWOBJ_FLAG_NONE));
    ObjArray *selfObj = AS_ARRAY(ret);
    ValueArray *ary = &selfObj->valAry;
    if (capaNum > 1) {
        initValueArrayWithCapa(ary, capaNum);
        writeValueArrayBulk(ary, 0, (size_t)capaNum, fill);
    } else {
        initValueArray(ary);
        writeValueArrayEnd(ary, fill);
    }
    OBJ_WRITE(ret, fill);
    return ret;
}


static Value lxArrayEach(int argCount, Value *args) {
    CHECK_ARITY("Array#each", 1, 1, argCount); // 2nd could be block arg (&arg)
    ObjArray *self = AS_ARRAY(*args);
    volatile ValueArray *ary = &self->valAry;
    volatile Value el; volatile int valIdx = 0;
    volatile int status = 0;
    volatile int iterStart = 0;
    volatile LxThread *th = vm.curThread;
    volatile BlockIterFunc fn = getFrame()->callInfo->blockIterFunc;
    volatile Obj *block = NULL;
    volatile ObjInstance *blockInstance = NULL;
    blockInstance = getBlockArg(getFrame());
    if (blockInstance) {
        block = blockCallableBlock(OBJ_VAL(blockInstance));
    }
    if (!block && getFrame()->callInfo) {
        block = (Obj*)(getFrame()->callInfo->blockFunction);
    }
    if (!block) {
        throwErrorFmt(lxErrClass, "no block given");
    }
    volatile BlockStackEntry *bentry = NULL;
    while (true) {
        if (iterStart == ary->count) {
            return *args;
        }
        SETUP_BLOCK(block, bentry, status, th->errInfo)
        if (status == TAG_NONE) {
            break;
        } else if (status == TAG_RAISE) {
            int iterFlags = 0;
            ObjInstance *errInst = AS_INSTANCE(th->lastErrorThrown);
            ASSERT(errInst);
            if (errInst->klass == lxBreakBlockErrClass) {
                return NIL_VAL;
            } else if (errInst->klass == lxContinueBlockErrClass) {
                if (fn) {
                    Value retVal = getProp(th->lastErrorThrown, INTERN("ret"));
                    fn(1, (Value*)&el, retVal, getFrame()->callInfo, &iterFlags);
                    if (iterFlags & ITER_FLAG_STOP) {
                        return NIL_VAL;
                    }
                }
            } else if (errInst->klass == lxReturnBlockErrClass) {
                Value retVal = getProp(th->lastErrorThrown, INTERN("ret"));
                if (fn) {
                    fn(1, (Value*)&el, retVal, getFrame()->callInfo, &iterFlags);
                    if (iterFlags & ITER_FLAG_STOP) {
                        return NIL_VAL;
                    }
                } else {
                    return retVal;
                }
            } else {
                throwError(th->lastErrorThrown);
            }
        }
    }

    VALARRAY_FOREACH_START(ary, el, iterStart, valIdx) {
        iterStart++;
        yieldFromC(1, (Value*)&el, TO_INSTANCE(blockInstance));
    }
    return *args;
}

static void mapIter(int argCount, Value *args, Value ret, CallInfo *cinfo, int *iterFlags) {
    DBG_ASSERT(cinfo->blockIterRet);
    arrayPush(*cinfo->blockIterRet, ret);
}

static Value lxArrayMap(int argCount, Value *args) {
    CHECK_ARITY("Array#map", 1, 1, argCount);
    Value self = *args;

    volatile Value ret = newArray();
    CallInfo cinfo;
    memset(&cinfo, 0, sizeof(CallInfo));
    cinfo.blockFunction = getFrame()->callInfo->blockFunction;
    cinfo.blockIterFunc = mapIter;
    cinfo.blockIterRet = &ret;
    cinfo.blockInstance = getBlockArg(getFrame());
    Value res = callMethod(AS_OBJ(self), INTERN("each"), 0, NULL, &cinfo);
    if (IS_NIL(res)) {
        return NIL_VAL;
    } else {
        return ret;
    }
}

static void selectIter(int argCount, Value *args, Value ret, CallInfo *cinfo, int *iterFlags) {
    if (isTruthy(ret)) {
        arrayPush(*cinfo->blockIterRet, *args);
    }
}

static Value lxArraySelect(int argCount, Value *args) {
    CHECK_ARITY("Array#select", 1, 1, argCount);
    Value self = *args;

    ObjFunction *block = getFrame()->callInfo->blockFunction;
    volatile Value ret = newArray();
    CallInfo cinfo;
    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.blockIterFunc = selectIter;
    cinfo.blockIterRet = &ret;
    cinfo.blockFunction = block;
    cinfo.blockInstance = getBlockArg(getFrame());
    Value res = callMethod(AS_OBJ(self), INTERN("each"), 0, NULL, &cinfo);
    if (IS_NIL(res)) {
        return res;
    } else {
        return ret;
    }
}

static void rejectIter(int argCount, Value *args, Value ret, CallInfo *cinfo, int *iterFlags) {
    if (!isTruthy(ret)) {
        arrayPush(*cinfo->blockIterRet, *args);
    }
}

static Value lxArrayReject(int argCount, Value *args) {
    CHECK_ARITY("Array#reject", 1, 1, argCount);
    Value self = *args;

    ObjFunction *block = getFrame()->callInfo->blockFunction;
    volatile Value ret = newArray();
    CallInfo cinfo;
    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.blockIterFunc = rejectIter;
    cinfo.blockIterRet = &ret;
    cinfo.blockFunction = block;
    cinfo.blockInstance = getBlockArg(getFrame());
    Value res = callMethod(AS_OBJ(self), INTERN("each"), 0, NULL, &cinfo);
    if (IS_NIL(res)) {
        return res;
    } else {
        return ret;
    }
}

static void findIter(int argCount, Value *args, Value ret, CallInfo *cinfo, int *iterFlags) {
    if (isTruthy(ret)) {
        *cinfo->blockIterRet = *args;
        *iterFlags |= ITER_FLAG_STOP;
    }
}

static Value lxArrayFind(int argCount, Value *args) {
    CHECK_ARITY("Array#find", 1, 1, argCount);
    Value self = *args;

    ObjFunction *block = getFrame()->callInfo->blockFunction;
    volatile Value ret = UNDEF_VAL;
    CallInfo cinfo;
    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.blockIterFunc = findIter;
    cinfo.blockIterRet = &ret;
    cinfo.blockFunction = block;
    cinfo.blockInstance = getBlockArg(getFrame());
    (void)callMethod(AS_OBJ(self), INTERN("each"), 0, NULL, &cinfo);
    if (IS_UNDEF(ret)) {
        return NIL_VAL;
    } else {
        return ret;
    }
}

static void reduceIter(int argCount, Value *args, Value ret, CallInfo *cinfo, int *iterFlags) {
    if (!IS_NUMBER(*args)) {
        throwErrorFmt(lxTypeErrClass, "Return value from reduce() must be a number");
    }
    *cinfo->blockIterRet = ret;
}

static Value lxArrayReduce(int argCount, Value *args) {
    CHECK_ARITY("Array#reduce", 2, 2, argCount);
    Value self = *args;

    ObjFunction *block = getFrame()->callInfo->blockFunction;
    volatile Value ret = args[1];
    CallInfo cinfo;
    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.blockIterFunc = reduceIter;
    cinfo.blockIterRet = &ret;
    cinfo.blockFunction = block;
    cinfo.blockInstance = getBlockArg(getFrame());
    cinfo.blockArgsExtra = &ret;
    cinfo.blockArgsNumExtra = 1;
    Value res = callMethod(AS_OBJ(self), INTERN("each"), 0, NULL, &cinfo);
    if (IS_NIL(res)) {
        return res;
    } else {
        return ret;
    }
}

static Value lxArraySum(int argCount, Value *args) {
    CHECK_ARITY("Array#sum", 1, 1, argCount);
    Value self = *args;
    double sum = 0.0;
    ObjArray *selfObj = AS_ARRAY(self);
    Value el; int elIdx = 0;
    VALARRAY_FOREACH(&selfObj->valAry, el, elIdx) {
        if (!IS_NUMBER(el)) {
            throwErrorFmt(lxTypeErrClass, "Element in summation is not a number");
        }
        sum += AS_NUMBER(el);
    }
    return NUMBER_VAL(sum);
}

static Value lxArrayReverse(int argCount, Value *args) {
    CHECK_ARITY("Array#reverse", 1, 1, argCount);
    Value self = *args;
    Value ret = newArray();
    ObjArray *selfObj = AS_ARRAY(self);
    Value el; int elIdx = 0;
    VALARRAY_FOREACH_REVERSE(&selfObj->valAry, el, elIdx) {
      arrayPush(ret, el);
    }
    return ret;
}

static Value lxArrayGetSize(int argCount, Value *args) {
    ValueArray *ary = &AS_ARRAY(*args)->valAry;
    return NUMBER_VAL(ary->count);
}

static Value lxArrayWrapStatic(int argCount, Value *args) {
    CHECK_ARITY("Array.wrap", 2, 2, argCount);
    if (IS_AN_ARRAY(args[1])) {
        return args[1];
    } else {
        Value ary = newArray();
        arrayPush(ary, args[1]);
        return ary;
    }
}

void Init_ArrayClass() {
    ObjClass *arrayClass = addGlobalClass("Array", lxObjClass);
    lxAryClass = arrayClass;
    ObjClass *arrayStatic = classSingletonClass(arrayClass);

    nativeArrayInit = addNativeMethod(arrayClass, "init", lxArrayInit);
    // static methods
    addNativeMethod(arrayStatic, "wrap", lxArrayWrapStatic);
    addNativeMethod(arrayStatic, "fill", lxArrayFillStatic);

    // methods
    addNativeMethod(arrayClass, "dup", lxArrayDup);
    addNativeMethod(arrayClass, "inspect", lxArrayInspect);
    addNativeMethod(arrayClass, "first", lxArrayFirst);
    addNativeMethod(arrayClass, "last", lxArrayLast);
    addNativeMethod(arrayClass, "push", lxArrayPush);
    addNativeMethod(arrayClass, "opShovelLeft", lxArrayPush);
    addNativeMethod(arrayClass, "pop", lxArrayPop);
    addNativeMethod(arrayClass, "pushFront", lxArrayPushFront);
    addNativeMethod(arrayClass, "popFront", lxArrayPopFront);
    addNativeMethod(arrayClass, "delete", lxArrayDelete);
    addNativeMethod(arrayClass, "deleteAt", lxArrayDeleteAt);
    addNativeMethod(arrayClass, "opIndexGet", lxArrayOpIndexGet);
    addNativeMethod(arrayClass, "opIndexSet", lxArrayOpIndexSet);
    addNativeMethod(arrayClass, "opEquals", lxArrayOpEquals);
    addNativeMethod(arrayClass, "toString", lxArrayToString);
    addNativeMethod(arrayClass, "sort", lxArraySort);
    addNativeMethod(arrayClass, "sortBy", lxArraySortBy);
    addNativeMethod(arrayClass, "iter", lxArrayIter);
    addNativeMethod(arrayClass, "clear", lxArrayClear);
    addNativeMethod(arrayClass, "join", lxArrayJoin);
    addNativeMethod(arrayClass, "hashKey", lxArrayHashKey);
    addNativeMethod(arrayClass, "each", lxArrayEach);
    addNativeMethod(arrayClass, "map", lxArrayMap);
    addNativeMethod(arrayClass, "select", lxArraySelect);
    addNativeMethod(arrayClass, "reject", lxArrayReject);
    addNativeMethod(arrayClass, "find", lxArrayFind);
    addNativeMethod(arrayClass, "reduce", lxArrayReduce);
    addNativeMethod(arrayClass, "sum", lxArraySum);
    addNativeMethod(arrayClass, "reverse", lxArrayReverse);

    // getters
    addNativeGetter(arrayClass, "size", lxArrayGetSize);
}
