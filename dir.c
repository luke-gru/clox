#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <glob.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"

ObjClass *lxDirClass = NULL;

typedef struct LxDir {
    DIR *dir;
    bool open;
} LxDir;

static inline LxDir *dirGetHidden(Value dir) {
    return (LxDir*)AS_INSTANCE(dir)->internal->data;
}

static void freeInternalDir(Obj *internalObj) {
    ASSERT(internalObj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)internalObj;
    LxDir *ldir = (LxDir*)internal->data;
    if (ldir->open) {
        closedir(ldir->dir);
    }
}

static Value lxDirInit(int argCount, Value *args) {
    CHECK_ARITY("Dir#init", 2, 2, argCount);
    ObjInstance *selfObj = AS_INSTANCE(*args);
    Value name = args[1];
    CHECK_ARG_IS_A(name, lxStringClass, 1);
    int last = errno;
    const char *dirStr = AS_STRING(name)->chars;
    DIR *d = opendir(dirStr);
    if (!d) {
        int err = errno;
        errno = last;
        throwArgErrorFmt("Given directory '%s' could not be opened: %s",
                dirStr, strerror(err));
    }
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxDir), NULL, freeInternalDir, NEWOBJ_FLAG_NONE);
    selfObj->internal = internalObj;
    LxDir *ldir = ALLOCATE(LxDir, 1);
    ldir->dir = d;
    ldir->open = true;
    internalObj->data = ldir;
    return *args;
}

static Value lxDirIterNext(int argCount, Value *args) {
    LxDir *dir = dirGetHidden(*args);
    struct dirent *de;
    if ((de = readdir(dir->dir))) {
        return OBJ_VAL(copyString(de->d_name, strlen(de->d_name), NEWOBJ_FLAG_NONE));
    } else {
        return NIL_VAL;
    }
}

static Value lxDirClose(int argCount, Value *args) {
    CHECK_ARITY("Dir#close", 1, 1, argCount);
    LxDir *ldir = dirGetHidden(*args);
    if (!ldir->open) {
        return FALSE_VAL;
    }
    int res = closedir(ldir->dir); // TODO: Should we throw here?
    ldir->open = false;
    return BOOL_VAL(res == 0);
}

static Value lxDirRewind(int argCount, Value *args) {
    CHECK_ARITY("Dir#rewind", 1, 1, argCount);
    LxDir *ldir = dirGetHidden(*args);
    if (!ldir->open) {
        return FALSE_VAL; // TODO: should we throw here?
    }
    rewinddir(ldir->dir);
    return TRUE_VAL;
}

static Value lxDirPwdStatic(int argCount, Value *args) {
    CHECK_ARITY("Dir.pwd", 1, 1, argCount);
    char buf[4096];
    int last = errno;
    char *res = getcwd(buf, 4096);
    if (!res) {
        int err = errno;
        errno = last;
        throwErrorFmt(sysErrClass(err), "Couldn't get current directory: %s", strerror(err));
    }
    return OBJ_VAL(copyString(buf, strlen(buf), NEWOBJ_FLAG_NONE));
}

static Value lxDirGlobStatic(int argCount, Value *args) {
    CHECK_ARITY("Dir.glob", 2, 2, argCount);
    Value globStrVal = args[1];
    CHECK_ARG_IS_A(globStrVal, lxStringClass, 1);
    char *globCStr = AS_STRING(globStrVal)->chars;

    glob_t globBuf;

    Value ary = newArray();
    int res = glob((const char *)globCStr, GLOB_BRACE, NULL, &globBuf);
    if (res == GLOB_NOMATCH) {
        globfree(&globBuf);
        return ary;
    } else if (res != 0) {
        globfree(&globBuf);
        // TODO: raise?
        return ary;
    }
    int i = 0;
    char *path = NULL;
    while ((path = globBuf.gl_pathv[i])) {
        Value pathVal = OBJ_VAL(copyString(path, strlen(path), NEWOBJ_FLAG_NONE));
        arrayPush(ary, pathVal);
        i++;
    }
    globfree(&globBuf);
    return ary;
}

static Value lxDirChdirStatic(int argCount, Value *args) {
    CHECK_ARITY("Dir.chdir", 2, 2, argCount);
    Value newDir = args[1];
    CHECK_ARG_IS_A(newDir, lxStringClass, 1);
    char *oldDir = NULL;
    char buf[4096];
    oldDir = getcwd(buf, 4096);
    char *dirStr = AS_STRING(newDir)->chars;
    int last = errno;
    int res = chdir(dirStr);
    if (res != 0) {
        int err = errno;
        errno = last;
        throwErrorFmt(sysErrClass(err), "Couldn't change directory to '%s': %s",
                dirStr,
                strerror(err));
    }
    Value ret = newDir;
    if (blockGiven()) {
        Value err = NIL_VAL;
        ret = yieldBlockCatch(0, NULL, &err);
        int res = 0;
        if (oldDir) {
            res = chdir(oldDir);
        }
        if (res != 0) {
            fprintf(stderr, "[Warning]: Couldn't change back to old directory: '%s'\n",
                    oldDir);
        }
        if (!IS_NIL(err)) {
            throwError(err);
        }
    }
    return ret;
}

void Init_DirClass() {
    lxDirClass = addGlobalClass("Dir", lxObjClass);
    addNativeMethod(lxDirClass, "init", lxDirInit);
    addNativeMethod(lxDirClass, "close", lxDirClose);
    addNativeMethod(lxDirClass, "rewind", lxDirRewind);
    addNativeMethod(lxDirClass, "iterNext", lxDirIterNext);
    ObjClass *dirStatic = classSingletonClass(lxDirClass);
    addNativeMethod(dirStatic, "pwd", lxDirPwdStatic);
    addNativeMethod(dirStatic, "chdir", lxDirChdirStatic);
    addNativeMethod(dirStatic, "glob", lxDirGlobStatic);
}
