#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"

ObjClass *lxDirClass = NULL;

static Value lxDirPwdStatic(int argCount, Value *args) {
    CHECK_ARITY("Dir.pwd", 1, 1, argCount);
    char buf[4096] = { '\0' };
    int last = errno;
    char *res = getcwd(buf, 4096);
    if (!res) {
        int err = errno;
        errno = last;
        throwErrorFmt(sysErrClass(err), "Couldn't get current directory: %s", strerror(err));
    }
    return OBJ_VAL(copyString(buf, strlen(buf), NEWOBJ_FLAG_NONE));
}

static Value lxDirChdirStatic(int argCount, Value *args) {
    CHECK_ARITY("Dir.chdir", 2, 3, argCount);
    Value newDir = args[1];
    CHECK_ARG_IS_A(newDir, lxStringClass, 1);
    Value callable = NIL_VAL;
    const char *oldDir = NULL;
    const char buf[4096] = { '\0' };
    if (argCount == 3) {
        callable = args[2];
        if (!isCallable(callable)) {
            throwErrorFmt(lxArgErrClass, "Second argument must be callable");
        }
        oldDir = getcwd(buf, 4096);
    }
    const char *dirStr = AS_STRING(newDir)->chars;
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
    if (!IS_NIL(callable)) {
        ret = callFunctionValue(callable, 0, NULL);
        if (oldDir) {
            int res = chdir(oldDir);
            if (res != 0) {
                fprintf(stderr, "[Warning]: Couldn't change back to old directory: '%s'\n",
                        oldDir);
            }
        }
    }
    return ret;
}

void Init_DirClass() {
    lxDirClass = addGlobalClass("Dir", lxObjClass);
    ObjClass *dirStatic = classSingletonClass(lxDirClass);
    addNativeMethod(dirStatic, "pwd", lxDirPwdStatic);
    addNativeMethod(dirStatic, "chdir", lxDirChdirStatic);
}
