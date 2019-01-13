#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/select.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"

ObjClass *lxIOClass;
#define READBUF_SZ 4092
#define WRITEBUF_SZ 4092

static void markInternalFile(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxFile *f = (LxFile*)internal->data;
    ASSERT(f);
    ASSERT(f->name);
    grayObject((Obj*)f->name);
}

static void freeInternalFile(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxFile *f = (LxFile*)internal->data;
    ASSERT(f);
    FREE(LxFile, f);
}

LxFile *fileGetHidden(Value io) {
    ObjInstance *inst = AS_INSTANCE(io);
    ObjInternal *internalObj = inst->internal;
    LxFile *f = (LxFile*)internalGetData(internalObj);
    return f;
}

LxFile *initIOAfterOpen(Value ioVal, ObjString *fname, int fd, int mode, int oflags) {
    ObjInstance *ioObj = AS_INSTANCE(ioVal);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxFile), markInternalFile, freeInternalFile,
            NEWOBJ_FLAG_NONE);
    hideFromGC((Obj*)ioObj);
    LxFile *file = ALLOCATE(LxFile, 1);
    file->name = dupString(fname);
    hideFromGC((Obj*)file->name);
    file->fd = fd;
    file->mode = mode;
    file->oflags = oflags;
    file->isOpen = true;
    internalObj->data = file;
    ioObj->internal = internalObj;
    unhideFromGC((Obj*)file->name);
    unhideFromGC((Obj*)ioObj);
    return file;
}

void IOClose(Value ioVal) {
    LxFile *f = FILE_GETHIDDEN(ioVal);
    ASSERT(f);
    if (f->isOpen) {
        int res = 0;
        int last = errno;
        if ((res = close(f->fd)) != 0) {
            int err = errno;
            errno = last;
            throwErrorFmt(sysErrClass(err), "Error closing fd: %d, %s", f->fd, strerror(err));
        }
        f->isOpen = false;
    }
}

static void NORETURN throwIOSyserr(int err, int last, const char *desc) {
    errno = last;
    throwErrorFmt(sysErrClass(err), "IO Error during %s: %s", desc, strerror(err));
}

ObjString *IOReadFd(int fd, size_t numBytes, bool untilEOF) {
    ObjString *retBuf = copyString("", 0, NEWOBJ_FLAG_NONE);
    size_t nread = 0;
    size_t justRead = 0;
    char fileReadBuf[READBUF_SZ];
    size_t maxRead = untilEOF ? READBUF_SZ-1 : (numBytes > (READBUF_SZ-1) ? (READBUF_SZ-1) : numBytes);
    int last = errno;
    releaseGVL();
    while ((justRead = read(fd, fileReadBuf, maxRead)) > 0) {
        acquireGVL();
        fileReadBuf[justRead] = '\0';
        pushCString(retBuf, fileReadBuf, justRead);
        releaseGVL();
        nread += justRead;
        numBytes -= justRead;
        maxRead = untilEOF ? READBUF_SZ : (numBytes > READBUF_SZ ? READBUF_SZ : numBytes);
    }
    acquireGVL();
    if (justRead == -1) {
        throwIOSyserr(errno, last, "read");
    }

    return retBuf;
}

ObjString *IOReadlineFd(int fd, size_t maxLen) {
    ObjString *retBuf = NULL;
    if (maxLen == 0) maxLen = READBUF_SZ;
    if (maxLen > READBUF_SZ) maxLen = READBUF_SZ;
    char fileReadBuf[READBUF_SZ];
    FILE *file = fdopen(fd, "r");
    ASSERT(file); // TODO: handle uncommon error, as it's already open this should not fail
    char *line = NULL;
    int last = errno;
    releaseGVL();
    line = fgets(fileReadBuf, maxLen, file);
    acquireGVL();
    if (line == NULL) {
        errno = last;
        return copyString("", 0, NEWOBJ_FLAG_NONE);
    }
    retBuf = copyString(line, strlen(line), NEWOBJ_FLAG_NONE);
    return retBuf;
}

ObjString *IORead(Value io, size_t numBytes, bool untilEOF) {
    LxFile *f = FILE_GETHIDDEN(io);
    if (!f->isOpen) {
        throwErrorFmt(lxErrClass, "IO error: cannot read from closed fd: %d", f->fd);
    }
    if (f->fd == STDOUT_FILENO || f->fd == STDERR_FILENO) {
        throwErrorFmt(lxErrClass, "Cannot read from stdout/stdin");
    }
    return IOReadFd(f->fd, numBytes, untilEOF);
}

ObjString *IOReadline(Value io, size_t maxBytes) {
    // TODO: checks
    LxFile *f = FILE_GETHIDDEN(io);
    return IOReadlineFd(f->fd, maxBytes);
}

size_t IOWrite(Value io, const void *buf, size_t count) {
    LxFile *f = FILE_GETHIDDEN(io);
    if (f->fd == STDIN_FILENO) {
        throwErrorFmt(lxErrClass, "Cannot write to stdin");
    }
    int fd = f->fd;
    size_t chunkSz = count > WRITEBUF_SZ ? WRITEBUF_SZ : count;
    char ioWritebuf[WRITEBUF_SZ];
    memcpy(ioWritebuf, buf, chunkSz);
    size_t written = 0;
    ssize_t res = 0;
    int last = errno;
    releaseGVL();
    while ((res = write(fd, ioWritebuf, chunkSz)) > 0 && (written += res) && written < count) {
        chunkSz -= written;
        memcpy(ioWritebuf, buf+written, chunkSz > WRITEBUF_SZ ? WRITEBUF_SZ : chunkSz);
    }
    acquireGVL();
    if (res == -1) {
        int err = errno;
        errno = last;
        throwErrorFmt(sysErrClass(err), "Error during write: %s", strerror(err));
    }
    return written;
}

int IOFcntl(Value io, int cmd, int arg) {
    int fd = FILE_GETHIDDEN(io)->fd;
    int last = errno;
    int res = fcntl(fd, cmd, arg);
    if (res == -1) {
        int err = errno;
        errno = last;
        throwErrorFmt(sysErrClass(err), "Error during fcntl for fd: %d, %s", fd, strerror(err));
    }
    return res;
}

Value lxIORead(int argCount, Value *args) {
    CHECK_ARITY("IO#read", 1, 2, argCount);
    Value self = args[0];
    bool untilEOF = false;
    double numBytesd;
    size_t numBytes = 0;
    if (argCount == 2) {
        CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
        numBytesd = AS_NUMBER(args[1]);
        if (numBytesd <= 0) {
            numBytes = 0;
            untilEOF = true;
        } else {
            numBytes = (size_t)numBytesd;
        }
    } else {
        untilEOF = true;
    }
    ObjString *buf = IORead(self, numBytes, untilEOF);
    ASSERT(buf);
    return OBJ_VAL(buf);
}

Value lxIOGetline(int argCount, Value *args) {
    CHECK_ARITY("IO#getline", 1, 2, argCount);
    Value self = args[0];
    size_t maxBytes = 0;
    if (argCount == 2) {
        CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
        double maxd = AS_NUMBER(args[1]);
        if (maxd > 0) {
            maxBytes = (size_t)maxd;
        }
    }
    ObjString *buf = IOReadline(self, maxBytes);
    return OBJ_VAL(buf);
}

/**
 * ex: var pipes = IO.pipe();
 * var reader = pipes[0];
 * var writer = pipes[1];
 */
Value lxIOPipeStatic(int argCount, Value *args) {
    // TODO: allow flags
    CHECK_ARITY("IO.pipe", 1, 1, argCount);
    int fds[2];
    int res = pipe(fds);
    int last = errno;
    if (res == -1) {
        int err = errno;
        errno = last;
        throwErrorFmt(sysErrClass(err), "Error creating pipes: %s", strerror(err));
    }
    Value ret = newArray();
    ObjInstance *reader = newInstance(lxIOClass, NEWOBJ_FLAG_NONE);
    Value readerVal = OBJ_VAL(reader);
    ObjInstance *writer = newInstance(lxIOClass, NEWOBJ_FLAG_NONE);
    // TODO: call init on IO both? Doesn't matter right now, not defined, but
    // should, it might get defined/redefined
    Value writerVal = OBJ_VAL(writer);
    initIOAfterOpen(readerVal, INTERN("reader (pipe)"), fds[0], 0, 0);
    initIOAfterOpen(writerVal, INTERN("writer (pipe)"), fds[1], 0, 0);
    arrayPush(ret, readerVal);
    arrayPush(ret, writerVal);
    return ret;
}

// IO.select(rds, wrs, errs, [timeout]);
Value lxIOSelectStatic(int argCount, Value *args) {
    CHECK_ARITY("IO.select", 4, 5, argCount);
    fd_set fds[3];
    FD_ZERO(&fds[0]);
    FD_ZERO(&fds[1]);
    FD_ZERO(&fds[2]);
    int highestFd = 0;
    ValueArray *valueArrays[3];
    for (int i = 1; i < 4; i++) {
        CHECK_ARG_IS_A(args[i], lxAryClass, i);
        ValueArray *ary = &AS_ARRAY(args[i])->valAry;
        Value el; int valIdx = 0;
        VALARRAY_FOREACH(ary, el, valIdx) {
            if (!IS_A(el, lxIOClass)) {
                throwErrorFmt(lxErrClass, "Non-IO object given to IO.select");
            }
            LxFile *f = FILE_GETHIDDEN(el);
            FD_SET(f->fd, &fds[i-1]);
            if (f->fd > highestFd) {
                highestFd = f->fd;
            }
        }
        valueArrays[i-1] = ary;
    }
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if (argCount == 5) {
        CHECK_ARG_BUILTIN_TYPE(args[4], IS_NUMBER_FUNC, "number", 5);
        int secs = (int)AS_NUMBER(args[4]);
        if (secs > 0) {
            timeout.tv_sec = secs;
        }
    }
    int last = errno;
    releaseGVL();
    int res = select(highestFd+1, &fds[0], &fds[1], &fds[2], &timeout);
    acquireGVL();
    if (res == -1) {
        int err = errno;
        errno = last;
        throwErrorFmt(sysErrClass(err), "Error from select: %s", strerror(err));
    }
    int numReady = res;
    if (numReady == 0) { // timeout
        return NIL_VAL;
    }

    Value ret = newArray();
    Value ioArrays[3];
    Value readersRdy = newArray();
    Value writersRdy = newArray();
    Value errorsRdy = newArray();

    ioArrays[0] = readersRdy;
    ioArrays[1] = writersRdy;
    ioArrays[2] = errorsRdy;

    arrayPush(ret, readersRdy);
    arrayPush(ret, writersRdy);
    arrayPush(ret, errorsRdy);
    if (res == 0) return ret;

    for (int i = 0; i < 3 && numReady > 0; i++) {
        ValueArray *ary = valueArrays[i];
        Value io; int valIdx = 0;
        VALARRAY_FOREACH(ary, io, valIdx) {
            LxFile *f = FILE_GETHIDDEN(io);
            if (FD_ISSET(f->fd, &fds[i])) {
                arrayPush(ioArrays[i], io);
                numReady--;
            }
        }
    }
    return ret;
}

Value lxIOWriteStatic(int argCount, Value *args) {
    CHECK_ARITY("IO.write", 3, 3, argCount);
    Value ioVal = args[1];
    CHECK_ARG_IS_A(ioVal, lxIOClass, 1);
    CHECK_ARG_IS_A(args[2], lxStringClass, 2);
    const char *buf = VAL_TO_STRING(args[2])->chars;
    size_t bufSz = strlen(buf);
    size_t written = IOWrite(ioVal, buf, bufSz);
    return NUMBER_VAL(written);
}

Value lxIOReadStatic(int argCount, Value *args) {
    CHECK_ARITY("IO.read", 2, 3, argCount);
    Value ioVal = args[1];
    CHECK_ARG_IS_A(ioVal, lxIOClass, 1);
    size_t bytes = 0;
    bool untilEOF = false;
    if (argCount == 3) {
        Value numBytes = args[2];
        CHECK_ARG_BUILTIN_TYPE(numBytes, IS_NUMBER_FUNC, "number", 2);
        double numBytesd = AS_NUMBER(numBytes);
        if (numBytesd <= 0) {
            bytes = 0;
            untilEOF = true;
        } else {
            bytes = (size_t)numBytesd;
        }
    } else {
        untilEOF = true;
    }
    ObjString *buf = IORead(ioVal, bytes, untilEOF);
    return OBJ_VAL(buf);
}

Value lxIOCloseStatic(int argCount, Value *args) {
    CHECK_ARITY("IO.close", 2, 2, argCount);
    Value ioVal = args[1];
    CHECK_ARG_IS_A(ioVal, lxIOClass, 1);
    IOClose(ioVal);
    return NIL_VAL;
}

Value lxIOWrite(int argCount, Value *args) {
    CHECK_ARITY("IO#read", 2, 2, argCount);
    CHECK_ARG_IS_A(args[1], lxStringClass, 1);
    Value self = *args;
    const char *buf = VAL_TO_STRING(args[1])->chars;
    return NUMBER_VAL(IOWrite(self, buf, strlen(buf)));
}

Value lxIOPuts(int argCount, Value *args) {
    CHECK_ARITY("IO#puts", 2, 2, argCount);
    CHECK_ARG_IS_A(args[1], lxStringClass, 1);
    Value self = *args;
    const char *buf = VAL_TO_STRING(args[1])->chars;
    IOWrite(self, buf, strlen(buf));
    IOWrite(self, "\n", 1);
    return NIL_VAL;
}

Value lxIOClose(int argCount, Value *args) {
    CHECK_ARITY("IO#close", 1, 1, argCount);
    IOClose(*args);
    return NIL_VAL;
}

// FIXME: only supports 2 arguments (cmd + arg1)
Value lxIOFcntl(int argCount, Value *args) {
    CHECK_ARITY("IO#fcntl", 2, 3, argCount);
    int cmd = 0;
    int arg = 0;
    CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
    Value self = args[0];
    cmd = (int)AS_NUMBER(args[1]);
    if (argCount == 3) {
        CHECK_ARG_BUILTIN_TYPE(args[2], IS_NUMBER_FUNC, "number", 2);
        arg = (int)AS_NUMBER(args[2]);
    }
    return NUMBER_VAL(IOFcntl(self, cmd, arg));
}

void Init_IOClass(void) {
    ObjClass *ioClass = addGlobalClass("IO", lxObjClass);
    lxIOClass = ioClass;
    ObjClass *ioStatic = singletonClass((Obj*)ioClass);

    addNativeMethod(ioStatic, "read", lxIOReadStatic);
    addNativeMethod(ioStatic, "write", lxIOWriteStatic);
    addNativeMethod(ioStatic, "close", lxIOCloseStatic);
    addNativeMethod(ioStatic, "pipe", lxIOPipeStatic);
    addNativeMethod(ioStatic, "select", lxIOSelectStatic);

    addNativeMethod(ioClass, "read", lxIORead);
    addNativeMethod(ioClass, "getline", lxIOGetline);
    addNativeMethod(ioClass, "write", lxIOWrite);
    addNativeMethod(ioClass, "puts", lxIOPuts);
    addNativeMethod(ioClass, "close", lxIOClose);
    addNativeMethod(ioClass, "fcntl", lxIOFcntl);

    // stdin/stdout/stderr
    ObjInstance *istdin = newInstance(ioClass, NEWOBJ_FLAG_OLD);
    Value stdinVal = OBJ_VAL(istdin);
    initIOAfterOpen(stdinVal, INTERN("stdin"), fileno(stdin), 0, O_RDONLY);

    ObjInstance *istdout = newInstance(ioClass, NEWOBJ_FLAG_OLD);
    Value stdoutVal = OBJ_VAL(istdout);
    initIOAfterOpen(stdoutVal, INTERN("stdout"), fileno(stdout), 0, O_WRONLY);

    ObjInstance *istderr = newInstance(ioClass, NEWOBJ_FLAG_OLD);
    Value stderrVal = OBJ_VAL(istderr);
    initIOAfterOpen(stderrVal, INTERN("stderr"), fileno(stderr), 0, O_WRONLY);

    tableSet(&vm.globals, OBJ_VAL(INTERN("stdin")), stdinVal);
    tableSet(&vm.globals, OBJ_VAL(INTERN("stdout")), stdoutVal);
    tableSet(&vm.globals, OBJ_VAL(INTERN("stderr")), stderrVal);

    // class properties
    Value ioClassVal = OBJ_VAL(ioClass);
    setProp(ioClassVal, INTERN("F_GETFD"), NUMBER_VAL(F_GETFD));
    setProp(ioClassVal, INTERN("F_SETFD"), NUMBER_VAL(F_SETFD));
    // the one and only file descriptor flag
    setProp(ioClassVal, INTERN("FD_CLOEXEC"), NUMBER_VAL(FD_CLOEXEC));

    // get file description status flags (O_xxx flags)
    setProp(ioClassVal, INTERN("F_GETFL"), NUMBER_VAL(F_SETFL));
    setProp(ioClassVal, INTERN("F_SETFL"), NUMBER_VAL(F_GETFL));
    // the few file description status flags to set in lox
    setProp(ioClassVal, INTERN("O_NONBLOCK"), NUMBER_VAL(O_NONBLOCK));
#ifndef O_DIRECT
#define O_DIRECT 0
#endif
    setProp(ioClassVal, INTERN("O_DIRECT"), NUMBER_VAL(O_DIRECT));
}
