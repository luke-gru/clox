#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"

ObjClass *lxStringClass;

extern ObjNative *nativeStringInit;

// ex: var s = "string";
// ex: var s2 = String("string");
Value lxStringInit(int argCount, Value *args) {
    CHECK_ARITY("String#init", 1, 2, argCount);
    callSuper(0, NULL, NULL);
    Value self = *args;
    ObjInstance *selfObj = AS_INSTANCE(self);
    if (argCount == 2) {
        Value internalStrVal = args[1];
        if (IS_T_STRING(internalStrVal)) { // string instance given, copy the buffer
            ObjString *orig = STRING_GETHIDDEN(internalStrVal);
            ObjString *new = dupString(orig);
            internalStrVal = OBJ_VAL(new);
        }
        if (!IS_STRING(internalStrVal)) { // other type given, convert to string
            ObjString *str = valueToString(internalStrVal, copyString);
            internalStrVal = OBJ_VAL(str);
        }
        ASSERT(IS_STRING(internalStrVal));
        tableSet(&selfObj->hiddenFields, OBJ_VAL(internedString("buf", 3)), internalStrVal);
    } else { // empty string
        Value internalStrVal = OBJ_VAL(copyString("", 0));
        tableSet(&selfObj->hiddenFields, OBJ_VAL(internedString("buf", 3)), internalStrVal);
    }
    return self;
}

static Value lxStringToString(int argCount, Value *args) {
    CHECK_ARITY("String#toString", 1, 1, argCount);
    return *args;
}

// ex: print "hi " + "there";
static Value lxStringOpAdd(int argCount, Value *args) {
    CHECK_ARITY("String#opAdd", 2, 2, argCount);
    Value self = *args;
    Value rhs = args[1];
    if (!IS_A_STRING(rhs)) {
        throwErrorFmt(lxTypeErrClass, "String#+ (opAdd) called with non-string argument. Type: %s",
                typeOfVal(rhs));
    }
    Value ret = dupStringInstance(self);
    ObjString *lhsBuf = STRING_GETHIDDEN(ret);
    ObjString *rhsBuf = STRING_GETHIDDEN(rhs);
    pushObjString(lhsBuf, rhsBuf);
    return ret;
}

// var s = "hey"; s.push(" there!"); => "hey there!"
static Value lxStringPush(int argCount, Value *args) {
    CHECK_ARITY("String#push", 2, 2, argCount);
    Value self = *args;
    Value rhs = args[1];
    CHECK_ARG_IS_A(rhs, lxStringClass, 1);
    pushString(self, rhs);
    return self;
}

// ex: var s = "hey"; var s2 = s.dup(); s.push(" again");
//     print s;  => "hey"
//     print s2; => "hey again"
static Value lxStringDup(int argCount, Value *args) {
    CHECK_ARITY("String#dup", 1, 1, argCount);
    Value ret = lxObjectDup(argCount, args);
    ObjInstance *retInst = AS_INSTANCE(ret);
    ObjString *buf = STRING_GETHIDDEN(ret);
    tableSet(&retInst->hiddenFields, OBJ_VAL(internedString("buf", 3)), OBJ_VAL(dupString(buf)));
    return ret;
}

// ex: var s = "going";
//     s.clear();
//     print s; => ""
static Value lxStringClear(int argCount, Value *args) {
    CHECK_ARITY("String#clear", 1, 1, argCount);
    clearString(*args);
    return *args;
}

static Value lxStringInsertAt(int argCount, Value *args) {
    CHECK_ARITY("String#insertAt", 3, 3, argCount);
    Value self = args[0];
    Value insert = args[1];
    Value at = args[2];
    CHECK_ARG_IS_A(insert, lxStringClass, 1);
    CHECK_ARG_BUILTIN_TYPE(at, IS_NUMBER_FUNC, "number", 2);
    stringInsertAt(self, insert, (int)AS_NUMBER(at));
    return self;
}

static Value lxStringSubstr(int argCount, Value *args) {
    CHECK_ARITY("String#substr", 3, 3, argCount);
    Value self = args[0];
    Value startIdx = args[1];
    Value len = args[2];
    CHECK_ARG_BUILTIN_TYPE(startIdx, IS_NUMBER_FUNC, "number", 1);
    CHECK_ARG_BUILTIN_TYPE(len, IS_NUMBER_FUNC, "number", 2);
    return stringSubstr(self, AS_NUMBER(startIdx), AS_NUMBER(len));
}

static Value lxStringOpIndexGet(int argCount, Value *args) {
    CHECK_ARITY("String#[]", 2, 2, argCount);
    Value self = args[0];
    Value index = args[1];
    CHECK_ARG_BUILTIN_TYPE(index, IS_NUMBER_FUNC, "number", 1);
    return stringIndexGet(self, AS_NUMBER(index));
}

static Value lxStringOpIndexSet(int argCount, Value *args) {
    CHECK_ARITY("String#[]=", 3, 3, argCount);
    Value self = args[0];
    Value index = args[1];
    CHECK_ARG_BUILTIN_TYPE(index, IS_NUMBER_FUNC, "number", 1);
    Value chrStr = args[1];
    CHECK_ARG_IS_A(chrStr, lxStringClass, 3);
    char chr = VAL_TO_STRING(chrStr)->chars[0];
    stringIndexSet(self, AS_NUMBER(index), chr);
    return self;
}

static Value lxStringOpEquals(int argCount, Value *args) {
    CHECK_ARITY("String#==", 2, 2, argCount);
    return BOOL_VAL(stringEquals(args[0], args[1]));
}

static Value lxStringGetSize(int argCount, Value *args) {
    ObjString *str = STRING_GETHIDDEN(*args);
    return NUMBER_VAL(str->length);
}

void Init_StringClass() {
    ObjClass *stringClass = addGlobalClass("String", lxObjClass);
    nativeStringInit = addNativeMethod(stringClass, "init", lxStringInit);
    // methods
    addNativeMethod(stringClass, "toString", lxStringToString);
    addNativeMethod(stringClass, "opAdd", lxStringOpAdd);
    addNativeMethod(stringClass, "opIndexGet", lxStringOpIndexGet);
    addNativeMethod(stringClass, "opIndexSet", lxStringOpIndexSet);
    addNativeMethod(stringClass, "opEquals", lxStringOpEquals);
    addNativeMethod(stringClass, "push", lxStringPush);
    addNativeMethod(stringClass, "opShovelLeft", lxStringPush);
    addNativeMethod(stringClass, "clear", lxStringClear);
    addNativeMethod(stringClass, "insertAt", lxStringInsertAt);
    addNativeMethod(stringClass, "substr", lxStringSubstr);
    addNativeMethod(stringClass, "dup", lxStringDup);

    // getters
    addNativeGetter(stringClass, "size", lxStringGetSize);
    lxStringClass = stringClass;
}
