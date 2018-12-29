#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "memory.h"
#include "table.h"

ObjClass *lxFileClass;

// TODO: make File a subclass of IO, and use per-IO object buffers
static char fileReadBuf[4096];

// Does this file exist and is it accessible? If not, returns errno from stat()
static int fileExists(char *fname) {
    struct stat buffer;
    int last = errno;
    if (stat(fname, &buffer) == 0) {
        return 0;
    } else {
        int ret = errno;
        errno = last;
        return ret;
    }
}

LxFile *fileGetHidden(Value file) {
    ObjInstance *inst = AS_INSTANCE(file);
    Value internalVal;
    if (tableGet(inst->hiddenFields, OBJ_VAL(internedString("f", 1)), &internalVal)) {
        DBG_ASSERT(IS_INTERNAL(internalVal));
        LxFile *f = (LxFile*)internalGetData(AS_INTERNAL(internalVal));
        return f;
    } else {
        return NULL;
    }
}

void fileClose(Value file) {
    LxFile *f = FILE_GETHIDDEN(file);
    ASSERT(f);
    if (f->isOpen) {
        int res = 0;
        int last = errno;
        if ((res = close(f->fd)) != 0) {
            int err = errno;
            errno = last;
            throwErrorFmt(lxErrClass, "Error closing file: %s", strerror(err));
        }
        f->isOpen = false;
    }
}

static int checkOpen(const char *fname, int flags, mode_t mode) {
    int last = errno;
    int fd = open(fname, flags, mode);
    if (fd < 0) {
        int err = errno;
        errno = last;
        const char *operation = "opening";
        if ((mode & O_CREAT) != 0) {
            operation = "creating";
        }
        throwErrorFmt(lxErrClass, "Error %s File '%s': %s", operation, fname, strerror(err));
    }
    return fd;
}

static FILE *checkFopen(const char *path, const char *modeStr) {
    int last = errno;
    FILE *f = fopen(path, modeStr);
    if (!f) {
        int err = errno;
        errno = last;
        throwErrorFmt(lxErrClass, "Error opening File '%s': %s", path, strerror(err));
    }
    return f;
}

static int checkFclose(FILE *stream) {
    int last = errno;
    int res = fclose(stream);
    errno = last;
    return res;
}

static void checkFerror(FILE *f, const char *op, const char *fname) {
    int last = errno;
    int readErr = ferror(f);
    if (readErr != 0) {
        int err = errno;
        errno = last;
        throwErrorFmt(lxErrClass, "Error %s File '%s': %s", op, fname, strerror(err));
    }
}

static void checkFileExists(const char *fname) {
    int err = 0;
    if ((err = fileExists(fname)) != 0) {
        if (err == EACCES) {
            throwArgErrorFmt("File '%s' not accessible", fname);
        } else {
            throwArgErrorFmt("File '%s' error: %s", fname, strerror(err));
        }
    }
}

static Value lxFileReadStatic(int argCount, Value *args) {
    CHECK_ARITY("File.read", 2, 2, argCount);
    Value fname = args[1];
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    ObjString *fnameStr = VAL_TO_STRING(fname);
    checkFileExists(fnameStr->chars);
    FILE *f = checkFopen(fnameStr->chars, "r");
    ObjString *retBuf = copyString("", 0);
    Value ret = newStringInstance(retBuf);
    size_t nread = 0;
    releaseGVL();
    while ((nread = fread(fileReadBuf, 1, sizeof(fileReadBuf), f)) > 0) {
        acquireGVL();
        pushCString(retBuf, fileReadBuf, nread);
        releaseGVL();
    }
    acquireGVL();
    checkFerror(f, "reading", fnameStr->chars);
    checkFclose(f);
    return ret;
}

static Value lxFileReadLinesStatic(int argCount, Value *args) {
    CHECK_ARITY("File.readLines", 2, 2, argCount);
    Value fname = args[1];
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    ObjString *fnameStr = VAL_TO_STRING(fname);
    checkFileExists(fnameStr->chars);
    FILE *f = checkFopen(fnameStr->chars, "r");
    Value ary = newArray();
    size_t nread = 0;
    releaseGVL();
    Value line;
    bool leftoverLine = false;
    while ((nread = fread(fileReadBuf, 1, sizeof(fileReadBuf), f)) > 0) {
        acquireGVL();
        size_t nleft = nread;
        char *bufp = fileReadBuf;
        char *bufpStart = bufp;
        while (nleft > 0 && *bufp) {
            while (*bufp != '\0' && *bufp != '\n') {
                bufp++;
                if (nleft == 0) break;
            }
            if (*bufp == '\n') bufp++;
            size_t len = bufp - bufpStart;
            if (leftoverLine) {
                pushCString(STRING_GETHIDDEN(line), bufpStart, len);
                leftoverLine = false;
            } else {
                line = newStringInstance(copyString(bufpStart, len));
                arrayPush(ary, line);
            }
            nleft -= len;
            if (!*bufp) {
                ASSERT(nleft == 0);
                leftoverLine = true;
                break; // read more data, reached end of buffer
            }
            bufpStart = bufp;
        }
        releaseGVL();
    }
    acquireGVL();
    checkFerror(f, "reading", fnameStr->chars);
    checkFclose(f);
    return ary;
}

static void markInternalFile(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxFile *f = (LxFile*)internal->data;
    ASSERT(f && f->name);
    grayObject((Obj*)f->name);
}

static void freeInternalFile(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxFile *f = (LxFile*)internal->data;
    FREE(LxFile, f);
}

static LxFile *initFile(Value fileVal, ObjString *fname, int fd, int flags) {
    ObjInstance *fileObj = AS_INSTANCE(fileVal);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxFile), markInternalFile, freeInternalFile);
    LxFile *file = ALLOCATE(LxFile, 1); // GCed by default GC free of internalObject
    file->name = dupString(fname);
    file->fd = fd;
    file->oflags = flags;
    file->isOpen = true;
    internalObj->data = file;
    fileObj->internal = internalObj;
    tableSet(fileObj->hiddenFields, OBJ_VAL(internedString("f", 1)), OBJ_VAL(internalObj));
    return file;
}

static Value lxFileInit(int argCount, Value *args) {
    CHECK_ARITY("File#init", 2, 2, argCount);
    Value self = args[0];
    Value fname = args[1];
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    ObjString *fnameStr = VAL_TO_STRING(fname);
    FILE *f = checkFopen(fnameStr->chars, "r+");
    int fd = fileno(f);
    initFile(self, fnameStr, fd, O_RDWR);
    return self;
}

static Value lxFileCreateStatic(int argCount, Value *args) {
    CHECK_ARITY("File.create", 2, 4, argCount);
    Value fname = args[1];
    // TODO: support flags and mode arguments
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    mode_t mode = 0664;
    int flags = O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC;
    ObjString *fnameStr = VAL_TO_STRING(fname);
    int fd = checkOpen(fnameStr->chars, flags, mode);
    Value file = OBJ_VAL(newInstance(lxFileClass));
    initFile(file, fnameStr, fd, flags);
    return file;
}

// Returns a File object for an opened file
static Value lxFileOpenStatic(int argCount, Value *args) {
    CHECK_ARITY("File.open", 2, 4, argCount);
    Value fname = args[1];
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    ObjString *fnameStr = VAL_TO_STRING(fname);
    int flags = O_RDWR|O_CLOEXEC;
    mode_t mode = 0644;
    if (argCount > 2) {
        Value flagsVal = args[2];
        CHECK_ARG_BUILTIN_TYPE(flagsVal, IS_NUMBER_FUNC, "number", 2);
        flags = (int)AS_NUMBER(flagsVal);
    }
    if (argCount > 3) {
        Value modeVal = args[3];
        CHECK_ARG_BUILTIN_TYPE(modeVal, IS_NUMBER_FUNC, "number", 3);
        mode = (mode_t)AS_NUMBER(modeVal);
    }
    int fd = checkOpen(fnameStr->chars, flags, mode);
    Value file = OBJ_VAL(newInstance(lxFileClass));
    initFile(file, fnameStr, fd, flags);
    return file;
}

static Value lxFileExistsStatic(int argCount, Value *args) {
    CHECK_ARITY("File.exists", 2, 2, argCount);
    Value fname = args[1];
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    return BOOL_VAL(fileExists(VAL_TO_STRING(fname)->chars) == 0);
}

static void checkFileWritable(LxFile *f) {
    if (!f->isOpen) {
        throwErrorFmt(lxErrClass, "File '%s' is not open", f->name->chars);
    }
    if ((f->oflags & O_RDWR) == 0 && (f->oflags & O_WRONLY) == 0) {
        throwErrorFmt(lxErrClass, "File '%s' is not open for writing", f->name->chars);
    }
}

static Value lxFileWrite(int argCount, Value *args) {
    CHECK_ARITY("File#write", 2, 3, argCount);
    Value self = args[0];
    LxFile *f = FILE_GETHIDDEN(self);
    checkFileWritable(f);
    Value toWrite = args[1];
    CHECK_ARG_IS_A(toWrite, lxStringClass, 1);
    const char *buf = VAL_TO_STRING(toWrite)->chars;
    size_t written = IOWrite(f->fd, buf, strlen(buf));
    return NUMBER_VAL(written);
}

// Close the file, if it isn't already closed
static Value lxFileClose(int argCount, Value *args) {
    CHECK_ARITY("File#close", 1, 1, argCount);
    Value self = args[0];
    fileClose(self);
    return NIL_VAL;
}

static Value lxFilePath(int argCount, Value *args) {
    CHECK_ARITY("File#path", 1, 1, argCount);
    Value self = args[0];
    LxFile *f = FILE_GETHIDDEN(self);
    return newStringInstance(dupString(f->name));
}

static Value lxFileUnlink(int argCount, Value *args) {
    CHECK_ARITY("File#unlink", 1, 1, argCount);
    Value self = args[0];
    LxFile *f = FILE_GETHIDDEN(self);
    int last = errno;
    if (unlink(f->name->chars) == 0) {
        f->isOpen = false;
        return BOOL_VAL(true);
    } else {
        int err = errno;
        errno = last;
        throwErrorFmt(lxErrClass, "Error during file unlink: %s", strerror(err));
    }
}

static Value lxFileRename(int argCount, Value *args) {
    CHECK_ARITY("File#rename", 2, 2, argCount);
    Value self = args[0];
    Value newName = args[1];
    CHECK_ARG_IS_A(newName, lxStringClass, 1);
    LxFile *f = FILE_GETHIDDEN(self);
    const char *oldPath = f->name->chars;
    ObjString *newPathStr = VAL_TO_STRING(newName);
    const char *newPath = newPathStr->chars;
    int last = errno;
    if (rename(oldPath, newPath) == 0) {
        f->name = dupString(newPathStr);
        return BOOL_VAL(true);
    } else {
        int err = errno;
        errno = last;
        throwErrorFmt(lxErrClass, "Error during file rename: %s", strerror(err));
    }
}

static Value lxFileSeek(int argCount, Value *args) {
    CHECK_ARITY("File#seek", 3, 3, argCount);
    Value self = args[0];
    Value offsetVal = args[1];
    Value whenceVal = args[2];
    CHECK_ARG_BUILTIN_TYPE(offsetVal, IS_NUMBER_FUNC, "number", 1);
    CHECK_ARG_BUILTIN_TYPE(whenceVal, IS_NUMBER_FUNC, "number", 2);
    off_t offset = (off_t)AS_NUMBER(offsetVal);
    off_t whence = (off_t)AS_NUMBER(whenceVal);
    LxFile *f = FILE_GETHIDDEN(self);
    int last = errno;
    off_t pos = lseek(f->fd, offset, whence);
    if (pos == -1) {
        int err = errno;
        errno = last;
        throwErrorFmt(lxErrClass, "Error during file seek: %s", strerror(err));
    }
    return NUMBER_VAL(pos);
}

static Value lxFileRewind(int argCount, Value *args) {
    CHECK_ARITY("File#rewind", 1, 1, argCount);
    Value seekArgs[2] = {
        NUMBER_VAL(0),
        NUMBER_VAL(SEEK_SET)
    };
    return callMethod(AS_OBJ(*args), internedString("seek", 4), 2, seekArgs);
}

void Init_FileClass(void) {
    ObjClass *fileClass = addGlobalClass("File", lxObjClass);
    ObjClass *fileStatic = classSingletonClass(fileClass);

    addNativeMethod(fileStatic, "create", lxFileCreateStatic);
    addNativeMethod(fileStatic, "open", lxFileOpenStatic);
    addNativeMethod(fileStatic, "exists", lxFileExistsStatic);
    addNativeMethod(fileStatic, "read", lxFileReadStatic);
    addNativeMethod(fileStatic, "readLines", lxFileReadLinesStatic);
    addNativeMethod(fileClass, "init", lxFileInit);
    addNativeMethod(fileClass, "write", lxFileWrite);
    addNativeMethod(fileClass, "close", lxFileClose);
    addNativeMethod(fileClass, "path", lxFilePath);
    addNativeMethod(fileClass, "unlink", lxFileUnlink);
    addNativeMethod(fileClass, "rename", lxFileRename);
    addNativeMethod(fileClass, "seek", lxFileSeek);
    addNativeMethod(fileClass, "rewind", lxFileRewind);

    Value fileClassVal = OBJ_VAL(fileClass);
    // TODO: make constants instead of properties
    setProp(fileClassVal, internedString("RDONLY", 6), NUMBER_VAL(O_RDONLY));
    setProp(fileClassVal, internedString("WRONLY", 6), NUMBER_VAL(O_WRONLY));
    setProp(fileClassVal, internedString("RDWR", 4), NUMBER_VAL(O_RDWR));
    setProp(fileClassVal, internedString("APPEND", 6), NUMBER_VAL(O_APPEND));
    setProp(fileClassVal, internedString("CREAT", 5), NUMBER_VAL(O_CREAT));
    setProp(fileClassVal, internedString("CLOEXEC", 7), NUMBER_VAL(O_CLOEXEC));
    setProp(fileClassVal, internedString("NOFOLLOW", 8), NUMBER_VAL(O_NOFOLLOW));
    setProp(fileClassVal, internedString("TMPFILE", 7), NUMBER_VAL(O_TMPFILE));
    setProp(fileClassVal, internedString("SYNC", 4), NUMBER_VAL(O_SYNC));
    setProp(fileClassVal, internedString("TRUNC", 5), NUMBER_VAL(O_TRUNC));
    setProp(fileClassVal, internedString("EXCL", 4), NUMBER_VAL(O_EXCL));

    setProp(fileClassVal, internedString("SEEK_SET", 8), NUMBER_VAL(SEEK_SET));
    setProp(fileClassVal, internedString("SEEK_CUR", 8), NUMBER_VAL(SEEK_CUR));
    setProp(fileClassVal, internedString("SEEK_END", 8), NUMBER_VAL(SEEK_END));
#ifndef SEEK_DATA
#define SEEK_DATA 0
#endif
    setProp(fileClassVal, internedString("SEEK_DATA", 9), NUMBER_VAL(SEEK_DATA));
#ifndef SEEK_HOLE
#define SEEK_HOLE 0
#endif
    setProp(fileClassVal, internedString("SEEK_HOLE", 9), NUMBER_VAL(SEEK_HOLE));


    lxFileClass = fileClass;
}
