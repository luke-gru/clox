#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"
#include <string.h>
#include <ctype.h>

ObjClass *lxStringClass;

ObjNative *nativeStringInit = NULL;

// ex: var s = "string";
// ex: var s2 = String("string");
static Value lxStringInit(int argCount, Value *args) {
    CHECK_ARITY("String#init", 1, 2, argCount);
    if (vm.inited) callSuper(0, NULL, NULL);
    Value self = *args;
    ObjString *selfStr = AS_STRING(self);
    ObjString *otherStr = NULL;
    if (argCount == 1) {
        otherStr = INTERN("");
    } else {
        if (!IS_STRING(args[1])) {
            otherStr = valueToString(args[1], copyString, NEWOBJ_FLAG_NONE);
        } else {
            otherStr = AS_STRING(args[1]);
        }
    }
    selfStr->capacity = otherStr->capacity;
    selfStr->hash = otherStr->hash;
    selfStr->length = otherStr->length;
    OBJ_UNSET_FROZEN(selfStr);
    if (STRING_IS_INTERNED(otherStr) && otherStr->chars) {
        selfStr->chars = otherStr->chars;
        STRING_SET_SHARED(selfStr);
    } else {
        if (otherStr->chars) {
            selfStr->chars = ALLOCATE(char, otherStr->capacity+1);
            memcpy(selfStr->chars, otherStr->chars, selfStr->length+1);
        } else {
            selfStr->chars = NULL;
        }
    }
    return *args;
}

static Value lxStringToString(int argCount, Value *args) {
    CHECK_ARITY("String#toString", 1, 1, argCount);
    return *args;
}

static Value lxStringInspect(int argCount, Value *args) {
    CHECK_ARITY("String#inspect", 1, 1, argCount);
    Value self = args[0];
    ObjString *selfStr = AS_STRING(self);
    ObjString *buf = emptyString();
    pushCString(buf, "\"", 1);
    size_t i = 0;
    while (i < selfStr->length) {
      if (selfStr->chars[i] == '\r') {
          pushCString(buf, "\\r", 2);
      } else if (selfStr->chars[i] == '\n') {
          pushCString(buf, "\\n", 2);
      } else if (selfStr->chars[i] == '\t') {
          pushCString(buf, "\\t", 2);
      } else if (selfStr->chars[i] == '"') {
          pushCString(buf, "\\\"", 2);
      } else {
          pushCString(buf, selfStr->chars+i, 1);
      }
      i++;
    }
    pushCString(buf, "\"", 1);
    return OBJ_VAL(buf);
}

// ex: print "hi " + "there";
static Value lxStringOpAdd(int argCount, Value *args) {
    CHECK_ARITY("String#opAdd", 2, 2, argCount);
    Value self = *args;
    Value rhs = args[1];
    if (UNLIKELY(!IS_STRING(rhs))) {
        throwErrorFmt(lxTypeErrClass, "String#+ (opAdd) called with non-string argument. Type: %s",
                typeOfVal(rhs));
    }
    ObjString *lhsBuf = dupString(AS_STRING(self));
    ObjString *rhsBuf = AS_STRING(rhs);
    pushObjString(lhsBuf, rhsBuf);
    return OBJ_VAL(lhsBuf);
}

static Value lxStringOpMul(int argCount, Value *args) {
    CHECK_ARITY("String#opMul", 2, 2, argCount);
    Value self = *args;
    Value rhs = args[1];
    if (UNLIKELY(!IS_NUMBER(rhs))) {
        throwErrorFmt(lxTypeErrClass, "String#* (opMul) called with non-number argument. Type: %s",
                typeOfVal(rhs));
    }
    ObjString *lhsBuf = dupString(AS_STRING(self));
    int num = AS_NUMBER(rhs);
    num--; // because "string" * 1 == "string"
    while (num > 0) {
      pushObjString(lhsBuf, AS_STRING(self));
      num--;
    }
    return OBJ_VAL(lhsBuf);
}

static inline void dedupString(ObjString *shared) {
    if (STRING_IS_SHARED(shared)) {
        char *cpy = shared->chars;
        shared->chars = ALLOCATE(char, shared->capacity+1);
        memcpy(shared->chars, cpy, shared->length+1);
        STRING_UNSET_SHARED(shared);
    }
}

// var s = "hey"; s.push(" there!"); => "hey there!"
static Value lxStringPush(int argCount, Value *args) {
    CHECK_ARITY("String#push", 2, 2, argCount);
    Value self = *args;
    Value rhs = args[1];
    CHECK_ARG_IS_A(rhs, lxStringClass, 1);
    dedupString(AS_STRING(self));
    pushString(self, rhs);
    return self;
}

// ex: var s = "hey"; var s2 = s.dup(); s.push(" again");
//     print s;  => "hey"
//     print s2; => "hey again"
static Value lxStringDup(int argCount, Value *args) {
    CHECK_ARITY("String#dup", 1, 1, argCount);
    Value self = *args;
    Value dup = callSuper(0, NULL, NULL);
    ObjString *selfStr = AS_STRING(self);
    ObjString *dupStr = AS_STRING(dup);
    STRING_UNSET_STATIC(dupStr);
    STRING_UNSET_INTERNED(dupStr);
    OBJ_UNSET_FROZEN(dupStr);
    dupStr->capacity = selfStr->capacity;
    dupStr->hash = selfStr->hash;
    dupStr->length = selfStr->length;
    dupStr->chars = ALLOCATE(char, dupStr->capacity+1);
    memcpy(dupStr->chars, selfStr->chars, dupStr->length+1);
    return dup;
}

// ex: var s = "going";
//     s.clear();
//     print s; => ""
static Value lxStringClear(int argCount, Value *args) {
    CHECK_ARITY("String#clear", 1, 1, argCount);
    Value self = *args;
    dedupString(AS_STRING(self));
    clearString(self);
    return self;
}

// NOTE: works on bytes, not codepoints for multibyte chars
static Value lxStringInsertAt(int argCount, Value *args) {
    CHECK_ARITY("String#insertAt", 3, 3, argCount);
    Value self = args[0];
    Value insert = args[1];
    Value at = args[2];
    CHECK_ARG_IS_A(insert, lxStringClass, 1);
    CHECK_ARG_BUILTIN_TYPE(at, IS_NUMBER_FUNC, "number", 2);
    dedupString(AS_STRING(self));
    stringInsertAt(self, insert, (int)AS_NUMBER(at), false);
    return self;
}

// NOTE: works on bytes, not codepoints for multibyte chars
static Value lxStringSubstr(int argCount, Value *args) {
    CHECK_ARITY("String#substr", 3, 3, argCount);
    Value self = args[0];
    Value startIdx = args[1];
    Value len = args[2];
    CHECK_ARG_BUILTIN_TYPE(startIdx, IS_NUMBER_FUNC, "number", 1);
    CHECK_ARG_BUILTIN_TYPE(len, IS_NUMBER_FUNC, "number", 2);
    return stringSubstr(self, AS_NUMBER(startIdx), AS_NUMBER(len));
}

// NOTE: works on bytes, not codepoints for multibyte chars
static Value lxStringOpIndexGet(int argCount, Value *args) {
    CHECK_ARITY("String#[]", 2, 2, argCount);
    Value self = args[0];
    Value index = args[1];
    CHECK_ARG_BUILTIN_TYPE(index, IS_NUMBER_FUNC, "number", 1);
    return stringIndexGet(self, AS_NUMBER(index));
}

// NOTE: works on bytes, not codepoints for multibyte chars
static Value lxStringOpIndexSet(int argCount, Value *args) {
    CHECK_ARITY("String#[]=", 3, 3, argCount);
    Value self = args[0];
    Value index = args[1];
    CHECK_ARG_BUILTIN_TYPE(index, IS_NUMBER_FUNC, "number", 1);
    Value chrStr = args[2];
    CHECK_ARG_IS_A(chrStr, lxStringClass, 3);
    dedupString(AS_STRING(self));
    // TODO: allow string longer than 1 char, or check size of given string
    char *chars = VAL_TO_STRING(chrStr)->chars;
    char chr = chars[0];
    if (strlen(chars) == 1) {
      stringIndexSet(self, AS_NUMBER(index), chr);
    } else {
      stringInsertAt(self, chrStr, (int)AS_NUMBER(index), true);
    }
    return self;
}

static Value lxStringOpEquals(int argCount, Value *args) {
    CHECK_ARITY("String#==", 2, 2, argCount);
    return BOOL_VAL(stringEquals(args[0], args[1]));
}

static Value lxStringSplit(int argCount, Value *args) {
    CHECK_ARITY("String#split", 2, 2, argCount);
    Value self = args[0];
    Value pat = args[1];
    CHECK_ARG_IS_A(pat, lxStringClass, 1);
    Value ret = newArray();
    char *hay = AS_CSTRING(self);
    char *needle = AS_CSTRING(pat);
    char *end = hay + strlen(hay);

    char *res = NULL;
    // ex: hay: "hello,,there", needle: ",,"
    // TODO: support regexes as pat
    while (hay < end && (res = strstr(hay, needle)) != NULL) {
      ObjString *part = copyString(hay, res - hay, NEWOBJ_FLAG_NONE);
      hay = res + strlen(needle);
      arrayPush(ret, OBJ_VAL(part));
    }
    if (hay != end) {
      ObjString *part = copyString(hay, end - hay, NEWOBJ_FLAG_NONE);
      arrayPush(ret, OBJ_VAL(part));
    }
    return ret;
}

static Value lxStringPadRight(int argCount, Value *args) {
    CHECK_ARITY("String#padRight", 3, 3, argCount);
    Value self = args[0];
    Value lenVal = args[1];
    Value padChar = args[2];
    CHECK_ARG_BUILTIN_TYPE(lenVal, IS_NUMBER_FUNC, "number", 1);
    CHECK_ARG_IS_A(padChar, lxStringClass, 2);
    ObjString *selfStr = AS_STRING(self);
    ObjString *padStr = AS_STRING(padChar);
    int newLen = AS_NUMBER(lenVal);
    int oldLen = selfStr->length;
    if (newLen <= oldLen) {
      return self;
    }
    int bytesToPad = newLen - oldLen;
    while (bytesToPad > 0) {
      pushCString(selfStr, padStr->chars, 1);
      bytesToPad--;
    }
    return self;
}

static Value lxStringRest(int argCount, Value *args) {
    CHECK_ARITY("String#rest", 2, 2, argCount);
    Value self = args[0];
    Value startVal = args[1];
    ObjString *selfStr = AS_STRING(self);
    CHECK_ARG_BUILTIN_TYPE(startVal, IS_NUMBER_FUNC, "number", 1);
    int start = AS_NUMBER(startVal);
    if (start < 0 || (size_t)start >= selfStr->length) {
      return OBJ_VAL(emptyString());
    }
    ObjString *ret = copyString(selfStr->chars+start, selfStr->length-start, NEWOBJ_FLAG_NONE);
    return OBJ_VAL(ret);
}

static Value lxStringGetSize(int argCount, Value *args) {
    ObjString *str = AS_STRING(*args);
    return NUMBER_VAL(str->length);
}

static Value lxStringEndsWith(int argCount, Value *args) {
    CHECK_ARITY("String#endsWith", 2, 2, argCount);
    Value self = args[0];
    Value endsPat = args[1];
    bool endsWith = false;
    CHECK_ARG_IS_A(endsPat, lxStringClass, 1);
    char *hay = AS_CSTRING(self);
    char *needle = AS_CSTRING(endsPat);
    if (strlen(needle) > strlen(hay)) {
      return BOOL_VAL(false);
    }
    char *hayEnd = hay + strlen(hay);
    char *hayStart = hayEnd - strlen(needle);
    if (strncmp(hayStart, needle, strlen(needle)) == 0) {
      endsWith = true;
    }

    return BOOL_VAL(endsWith);
}

static Value lxStringCompact(int argCount, Value *args) {
    CHECK_ARITY("String#compact", 1, 1, argCount);
    Value self = args[0];
    char *orig = AS_CSTRING(self);
    ObjString *new = copyString("", 0, NEWOBJ_FLAG_NONE);
    char *start = orig;
    char *end = start + strlen(orig)-1;
    while (start < end && isspace(*start)) {
      start++;
    }
    while (end > start && isspace(*end)) {
      end--;
    }
    if (start < end) {
      pushCString(new, start, end-start+1);
    }
    return OBJ_VAL(new);
}

static Value lxStringCompactLeft(int argCount, Value *args) {
    CHECK_ARITY("String#compactLeft", 1, 1, argCount);
    Value self = args[0];
    char *orig = AS_CSTRING(self);
    ObjString *new = copyString("", 0, NEWOBJ_FLAG_NONE);
    char *start = orig;
    char *end = start + strlen(orig)-1;
    while (start < end && isspace(*start)) {
      start++;
    }
    if (start < end) {
      pushCString(new, start, end-start+1);
    }
    return OBJ_VAL(new);
}

static Value lxStringIndex(int argCount, Value *args) {
    CHECK_ARITY("String#index", 2, 2, argCount);
    Value self = args[0];
    Value needleVal = args[1];
    char *hay = AS_CSTRING(self);
    char *needle = AS_CSTRING(needleVal);
    char *found = strstr(hay, needle);
    if (found == NULL) {
      return NIL_VAL;
    } else {
      return NUMBER_VAL(found-hay);
    }
}

static Value lxStringStaticParseInt(int argCount, Value *args) {
    CHECK_ARITY("String.parseInt", 2, 2, argCount);
    Value str = args[1];
    CHECK_ARG_IS_A(str, lxStringClass, 1);
    char *chars = AS_CSTRING(str);
    return NUMBER_VAL(atoi(chars));
}

void Init_StringClass() {
    ObjClass *stringClass = addGlobalClass("String", lxObjClass);
    lxStringClass = stringClass;
    ObjClass *stringClassStatic = classSingletonClass(stringClass);
    nativeStringInit = addNativeMethod(stringClass, "init", lxStringInit);

    // static methods
    addNativeMethod(stringClassStatic, "parseInt", lxStringStaticParseInt);

    // methods
    addNativeMethod(stringClass, "toString", lxStringToString);
    addNativeMethod(stringClass, "inspect", lxStringInspect);
    addNativeMethod(stringClass, "opAdd", lxStringOpAdd);
    addNativeMethod(stringClass, "opMul", lxStringOpMul);
    addNativeMethod(stringClass, "opIndexGet", lxStringOpIndexGet);
    addNativeMethod(stringClass, "opIndexSet", lxStringOpIndexSet);
    addNativeMethod(stringClass, "opEquals", lxStringOpEquals);
    addNativeMethod(stringClass, "push", lxStringPush);
    addNativeMethod(stringClass, "opShovelLeft", lxStringPush);
    addNativeMethod(stringClass, "clear", lxStringClear);
    addNativeMethod(stringClass, "insertAt", lxStringInsertAt);
    addNativeMethod(stringClass, "substr", lxStringSubstr);
    addNativeMethod(stringClass, "dup", lxStringDup);
    addNativeMethod(stringClass, "split", lxStringSplit);
    addNativeMethod(stringClass, "endsWith", lxStringEndsWith);
    addNativeMethod(stringClass, "compact", lxStringCompact);
    addNativeMethod(stringClass, "compactLeft", lxStringCompactLeft);
    addNativeMethod(stringClass, "padRight", lxStringPadRight);
    addNativeMethod(stringClass, "rest", lxStringRest);
    addNativeMethod(stringClass, "index", lxStringIndex);
    // TODO: add startsWith, rindex

    // getters
    addNativeGetter(stringClass, "size", lxStringGetSize);
}
