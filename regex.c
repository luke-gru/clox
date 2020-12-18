#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"
#include "compiler.h"
#include "regex_lib.h"

ObjClass *lxRegexClass = NULL;
ObjClass *lxRegexErrClass = NULL;
ObjNative *nativeRegexInit = NULL;

static Value lxRegexInit(int argCount, Value *args) {
    CHECK_ARITY("Regex#init", 2, 2, argCount);
    Value reStr = args[1];
    CHECK_ARG_IS_A(reStr, lxStringClass, 1);
    Regex *re = ALLOCATE(Regex, 1);
    regex_init(re, AS_STRING(reStr)->chars, NULL);
    AS_REGEX(*args)->regex = re;
    RegexCompileResult compRes = regex_compile(re);
    if (compRes != REGEX_COMPILE_SUCCESS) {
        // this object will be GCed and the `regex` struct will be freed then
        throwErrorFmt(lxRegexErrClass, "Regex compilation error");
    }
    return *args;
}

static Value lxRegexInspect(int argCount, Value *args) {
    CHECK_ARITY("Regex#inspect", 1, 1, argCount);
    Value self = args[0];
    Regex *re = AS_REGEX(self)->regex;
    ObjString *buf = emptyString();
    pushCStringFmt(buf, "%s", "#<Regex %\"");
    pushCString(buf, re->src, strlen(re->src));
    pushCString(buf, "\">", 2);
    return OBJ_VAL(buf);
}

static Value lxRegexMatch(int argCount, Value *args) {
    CHECK_ARITY("Regex#match", 2, 2, argCount);
    Value str = args[1];
    CHECK_ARG_IS_A(str, lxStringClass, 1);
    Regex *re = AS_REGEX(*args)->regex;
    DBG_ASSERT(re);
    MatchData mdata = regex_match(re, AS_STRING(str)->chars);
    if (mdata.matched) {
        return NUMBER_VAL(mdata.match_start);
    } else {
        return NIL_VAL;
    }
}

void Init_RegexClass() {
    lxRegexClass = addGlobalClass("Regex", lxObjClass);
    lxRegexErrClass = addGlobalClass("RegexError", lxErrClass);

    nativeRegexInit = addNativeMethod(lxRegexClass, "init", lxRegexInit);
    addNativeMethod(lxRegexClass, "inspect", lxRegexInspect);
    addNativeMethod(lxRegexClass, "match", lxRegexMatch);
}
