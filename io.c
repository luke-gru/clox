#include <unistd.h>
#include <errno.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"

ObjModule *lxIOMod;

#define READBUF_SZ 8192
char ioReadbuf[READBUF_SZ];
#define WRITEBUF_SZ 8192
char ioWritebuf[WRITEBUF_SZ];

/**
 * ex: var pipes = IO.pipe();
 * var readerFd = pipes[0];
 * var writerFd = pipes[1];
 */
Value IOPipeStatic(int argCount, Value *args) {
    // TODO: allow flags
    CHECK_ARITY("IO.pipe", 1, 1, argCount);
    int fds[2];
    int res = pipe(fds);
    int last = errno;
    if (res == -1) {
        int err = errno;
        errno = last;
        throwErrorFmt(lxErrClass, "Error creating pipes: %s", strerror(err));
    }
    Value ret = newArray();
    arrayPush(ret, NUMBER_VAL(fds[0]));
    arrayPush(ret, NUMBER_VAL(fds[1]));
    return ret;
}

Value IOWriteStatic(int argCount, Value *args) {
    CHECK_ARITY("IO.write", 3, 3, argCount);
    CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
    CHECK_ARG_IS_A(args[2], lxStringClass, 2);
    int fd = (int)AS_NUMBER(args[1]);
    const char *buf = VAL_TO_STRING(args[2])->chars;
    size_t bufSz = strlen(buf);
    memcpy(ioWritebuf, buf, bufSz > WRITEBUF_SZ ? WRITEBUF_SZ : bufSz);
    size_t written = 0;
    ssize_t res = 0;
    int last = errno;
    releaseGVL();
    while ((res = write(fd, ioWritebuf, bufSz)) > 0 && res < bufSz) {
        acquireGVL();
        written += res;
        bufSz -= written;
        // FIXME: buf size could be changed by another thread, maybe copy at beginning or
        // do strlen() again to check length?
        memcpy(ioWritebuf, buf+written, bufSz > WRITEBUF_SZ ? WRITEBUF_SZ : bufSz);
        releaseGVL();
    }
    acquireGVL();
    if (res == -1) {
        int err = errno;
        errno = last;
        throwErrorFmt(lxErrClass, "Error during write: %s", strerror(err));
    }
    written += res;
    return NUMBER_VAL(written);
}


Value IOReadStatic(int argCount, Value *args) {
    CHECK_ARITY("IO.read", 2, 2, argCount);
    CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
    int fd = (int)AS_NUMBER(args[1]);
    int last = errno;
    int res = 0;
    char *buf = ioReadbuf;
    size_t bytesRead = 0;
    Value ret = newStringInstance(copyString("", 0));
    ObjString *retStr = STRING_GETHIDDEN(ret);
    releaseGVL();
    while ((res = read(fd, buf, READBUF_SZ)) == READBUF_SZ) {
        bytesRead += res;
        acquireGVL();
        pushCString(retStr, ioReadbuf, res);
        releaseGVL();
    }
    acquireGVL();
    if (res == -1) {
        int err = errno;
        errno = last;
        throwErrorFmt(lxErrClass, "Error during read: %s", strerror(err));
    }
    bytesRead += res;
    pushCString(retStr, ioReadbuf, res);
    return ret;
}

Value IOCloseStatic(int argCount, Value *args) {
    CHECK_ARITY("IO.close", 2, 2, argCount);
    CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
    int fd = (int)AS_NUMBER(args[1]);
    int last = errno;
    int res = close(fd);
    if (res == -1) {
        int err = errno;
        errno = last;
        throwErrorFmt(lxErrClass, "Error during close: %s", strerror(err));
    }
    return BOOL_VAL(true);
}

void Init_IOModule(void) {
    ObjModule *ioMod = addGlobalModule("IO");
    lxIOMod = ioMod;
    ObjClass *ioStatic = moduleSingletonClass(ioMod);

    addNativeMethod(ioStatic, "pipe", IOPipeStatic);
    addNativeMethod(ioStatic, "write", IOWriteStatic);
    addNativeMethod(ioStatic, "read", IOReadStatic);
    addNativeMethod(ioStatic, "close", IOCloseStatic);
}
