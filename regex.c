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
ObjClass *lxMatchDataClass = NULL;

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

static LxMatchData *getMatchData(Value self) {
    ObjInstance *selfInst = AS_INSTANCE(self);
    ObjInternal *internal = selfInst->internal;
    return (LxMatchData*)internal->data;
}

static void matchDataSetRegex(Value md, Value regex) {
  LxMatchData *lmd = getMatchData(md);
  lmd->re = AS_REGEX(regex);
}

static void matchDataPopulateCaptures(Value md) {
    LxMatchData *lmd = getMatchData(md);
    Regex *re = lmd->re->regex;
    GroupNode *gn = re->groups;
    Value captures = newArray();
    while (gn) {
        if (gn->group->capture_beg) {
            ObjString *capture = copyString(gn->group->capture_beg,
                    gn->group->capture_end-gn->group->capture_beg+1,
                    NEWOBJ_FLAG_NONE);
            arrayPush(captures, OBJ_VAL(capture));
        } else {
            break;
        }
        gn = gn->next;
    }
    lmd->captures = captures;
}

static Value lxRegexMatch(int argCount, Value *args) {
    CHECK_ARITY("Regex#match", 2, 3, argCount);
    Value self = args[0];
    Value str = args[1];
    CHECK_ARG_IS_A(str, lxStringClass, 1);
    bool giveMatchData = false;
    if (argCount == 3) {
      giveMatchData = isTruthy(args[2]);
    }
    Regex *re = AS_REGEX(*args)->regex;
    DBG_ASSERT(re);
    MatchData mdata = regex_match(re, AS_STRING(str)->chars);
    if (mdata.matched) {
      if (giveMatchData) {
          Value mdArgs[2] = {
              NUMBER_VAL(mdata.match_start),
              NUMBER_VAL(mdata.match_len)
          };
          Value md = callFunctionValue(OBJ_VAL(lxMatchDataClass), 2, mdArgs);
          matchDataSetRegex(md, self);
          matchDataPopulateCaptures(md);
          return md;
        } else {
            return NUMBER_VAL(mdata.match_start);
        }
    } else {
        return NIL_VAL;
    }
}

static void markInternalMatchData(Obj *obj) {
    ObjInternal *internal = (ObjInternal*)obj;
    LxMatchData *md = internal->data;
    if (md->re) {
        grayObject(TO_OBJ(md->re));
    }
    if (md->captures != NIL_VAL) {
        grayObject(TO_OBJ(md->captures));
    }
}

static void freeInternalMatchData(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    FREE(LxMatchData, internal->data);
}

static Value lxMatchDataInit(int argCount, Value *args) {
    CHECK_ARITY("MatchData#init", 3, 3, argCount);
    Value self = args[0];
    Value start = args[1];
    Value len = args[2];
    CHECK_ARG_BUILTIN_TYPE(start, IS_NUMBER_FUNC, "number", 1);
    CHECK_ARG_BUILTIN_TYPE(len, IS_NUMBER_FUNC, "number", 2);
    ObjInstance *selfInst = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(MatchData), markInternalMatchData, freeInternalMatchData,
            NEWOBJ_FLAG_NONE);
    hideFromGC((Obj*)internalObj);
    LxMatchData *lmd = ALLOCATE(LxMatchData, 1);
    lmd->md.match_start = AS_NUMBER(start);
    lmd->md.match_len = AS_NUMBER(len);
    lmd->re = NULL;
    lmd->captures = NIL_VAL;
    internalObj->data = lmd;
    selfInst->internal = internalObj;
    unhideFromGC((Obj*)internalObj);
    return self;
}

static Value lxMatchDataCaptures(int argCount, Value *args) {
    CHECK_ARITY("MatchData#captures", 1, 1, argCount);
    Value self = args[0];
    LxMatchData *lmd = getMatchData(self);
    if (!lmd->re) {
      return newArray();
    }
    if (lmd->captures == NIL_VAL) {
        return newArray();
    }
    return lmd->captures;
}

static Value lxMatchDataStart(int argCount, Value *args) {
    Value self = args[0];
    MatchData *md = getMatchData(self);
    return NUMBER_VAL(md->match_start);
}

static Value lxMatchDataLength(int argCount, Value *args) {
    Value self = args[0];
    MatchData *md = getMatchData(self);
    return NUMBER_VAL(md->match_len);
}

void Init_RegexClass() {
    lxRegexClass = addGlobalClass("Regex", lxObjClass);
    lxRegexErrClass = addGlobalClass("RegexError", lxErrClass);

    nativeRegexInit = addNativeMethod(lxRegexClass, "init", lxRegexInit);
    addNativeMethod(lxRegexClass, "inspect", lxRegexInspect);
    addNativeMethod(lxRegexClass, "match", lxRegexMatch);

    ObjClass *matchDataClass = addGlobalClass("MatchData", lxObjClass);
    lxMatchDataClass = matchDataClass;
    addNativeMethod(matchDataClass, "init", lxMatchDataInit);
    addNativeGetter(matchDataClass, "start", lxMatchDataStart);
    addNativeGetter(matchDataClass, "length", lxMatchDataLength);
    addNativeGetter(matchDataClass, "captures", lxMatchDataCaptures);
}
