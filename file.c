#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "memory.h"
#include "table.h"

ObjClass *lxFileClass;
static ObjClass *lxFilePasswdClass;
static ObjClass *lxFileGroupClass;
static ObjClass *lxFileStatClass;
#define READBUF_SZ 4092
#define WRITEBUF_SZ 4092

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

static int checkOpen(const char *fname, int flags, mode_t mode) {
    int last = errno;
    releaseGVL(THREAD_STOPPED);
    int fd = open(fname, flags, mode);
    acquireGVL();
    if (fd < 0) {
        int err = errno;
        errno = last;
        const char *operation = "opening";
        if ((mode & O_CREAT) != 0) {
            operation = "creating";
        }
        throwErrorFmt(sysErrClass(err), "Error %s File '%s': %s", operation, fname, strerror(err));
    }
    return fd;
}

static FILE *checkFopen(const char *path, const char *modeStr) {
    int last = errno;
    releaseGVL(THREAD_STOPPED);
    FILE *f = fopen(path, modeStr);
    acquireGVL();
    if (!f) {
        int err = errno;
        errno = last;
        throwErrorFmt(sysErrClass(err), "Error opening File '%s': %s", path, strerror(err));
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
        throwErrorFmt(sysErrClass(err), "Error %s File '%s': %s", op, fname, strerror(err));
    }
}

static void checkFileExists(char *fname) {
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
    ObjString *buf = IOReadFd(fileno(f), 0, true);
    checkFclose(f);
    return OBJ_VAL(buf);
}

static Value lxFileStatStatic(int argCount, Value *args) {
    CHECK_ARITY("File.stat", 2, 2, argCount);
    Value path = args[1];
    Value ret = callFunctionValue(OBJ_VAL(lxFileStatClass), 1, &path);
    return ret;
}

static Value lxFileUserStatic(int argCount, Value *args) {
    CHECK_ARITY("File.user", 2, 2, argCount);
    Value user = args[1];
    Value ret = callFunctionValue(OBJ_VAL(lxFilePasswdClass), 1, &user);
    return ret;
}

static Value lxFileGroupStatic(int argCount, Value *args) {
    CHECK_ARITY("File.group", 2, 2, argCount);
    Value group = args[1];
    Value ret = callFunctionValue(OBJ_VAL(lxFileGroupClass), 1, &group);
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
    releaseGVL(THREAD_STOPPED);
    Value line = NIL_VAL;
    bool leftoverLine = false;
    char fileReadBuf[READBUF_SZ];
    while ((nread = fread(fileReadBuf, 1, sizeof(fileReadBuf), f)) > 0) {
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
                pushCString(AS_STRING(line), bufpStart, len);
                leftoverLine = false;
            } else {
                line = OBJ_VAL(copyString(bufpStart, len, NEWOBJ_FLAG_NONE));
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
    }
    acquireGVL();
    checkFerror(f, "reading", fnameStr->chars);
    checkFclose(f);
    return ary;
}

static Value lxFileStaticIsDir(int argCount, Value *args) {
    CHECK_ARITY("File.isDir", 2, 2, argCount);
    Value path = args[1];
    CHECK_ARG_IS_A(path, lxStringClass, 1);
    struct stat st;
    releaseGVL(THREAD_STOPPED);
    int res = stat(AS_CSTRING(path), &st);
    acquireGVL();
    if (res != 0) {
      throwErrorFmt(sysErrClass(errno), "Error during stat for file %s: %s", AS_CSTRING(path), strerror(errno));
    }
    return BOOL_VAL(S_ISDIR(st.st_mode));
}

static Value lxFileStaticCopy(int argCount, Value *args) {
    CHECK_ARITY("File.copy", 3, 3, argCount);
    Value src = args[1];
    Value dst = args[2];
    CHECK_ARG_IS_A(src, lxFileClass, 1);
    CHECK_ARG_IS_A(dst, lxFileClass, 2);
    LxFile *srcf = FILE_GETHIDDEN(src);
    LxFile *dstf = FILE_GETHIDDEN(dst);
    size_t chunkSz = 1024 * 16;
    ssize_t res = 0;
    ssize_t bytesCopied = 0;
    char *buf = ALLOCATE(char, chunkSz);
    releaseGVL(THREAD_STOPPED);
    while ((res = read(srcf->fd, buf, chunkSz)) > 0) {
      int wres = write(dstf->fd, buf, res);
      if (wres != res) {
        FREE_SIZE(chunkSz, buf);
        throwErrorFmt(sysErrClass(errno), "Error during write: %s", strerror(errno));
      }
      bytesCopied += res;
    }
    acquireGVL();
    FREE_SIZE(chunkSz, buf);
    return NUMBER_VAL(bytesCopied);
}

static Value lxFileStaticSymlink(int argCount, Value *args) {
    CHECK_ARITY("File.symlink", 3, 3, argCount);
    Value target = args[1];
    Value linkpath = args[2];
    CHECK_ARG_IS_A(target, lxStringClass, 1);
    CHECK_ARG_IS_A(linkpath, lxStringClass, 2);

    int res = symlink(AS_CSTRING(target), AS_CSTRING(linkpath));
    if (res != 0) {
        throwErrorFmt(sysErrClass(errno), "Error during symlink: %s", strerror(errno));
    }
    return TRUE_VAL;
}

static Value lxFileInit(int argCount, Value *args) {
    CHECK_ARITY("File#init", 2, 2, argCount);
    callSuper(0, NULL, NULL);
    Value self = args[0];
    Value fname = args[1];
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    ObjString *fnameStr = VAL_TO_STRING(fname);
    FILE *f = checkFopen(fnameStr->chars, "r+");
    int fd = fileno(f);
    initIOAfterOpen(self, fnameStr, fd, 0, O_RDWR);
    return self;
}

static Value lxFileCreateStatic(int argCount, Value *args) {
    CHECK_ARITY("File.create", 2, 3, argCount);
    Value fname = args[1];
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    mode_t mode = 0664;
    int flags = O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC;
    if (argCount == 3) {
      Value modeVal = args[2];
      CHECK_ARG_BUILTIN_TYPE(modeVal, IS_NUMBER_FUNC, "number", 2);
      mode = AS_NUMBER(modeVal);
    }
    ObjString *fnameStr = VAL_TO_STRING(fname);
    int fd = checkOpen(fnameStr->chars, flags, mode);
    Value file = OBJ_VAL(newInstance(lxFileClass, NEWOBJ_FLAG_NONE));
    initIOAfterOpen(file, fnameStr, fd, mode, flags);
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
    Value file = OBJ_VAL(newInstance(lxFileClass, NEWOBJ_FLAG_NONE));
    initIOAfterOpen(file, fnameStr, fd, mode, flags);
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
    size_t written = IOWrite(self, buf, strlen(buf));
    return NUMBER_VAL(written);
}

// Close the file, if it isn't already closed
static Value lxFileClose(int argCount, Value *args) {
    CHECK_ARITY("File#close", 1, 1, argCount);
    Value self = args[0];
    IOClose(self);
    return NIL_VAL;
}

static Value lxFilePath(int argCount, Value *args) {
    CHECK_ARITY("File#path", 1, 1, argCount);
    Value self = args[0];
    LxFile *f = FILE_GETHIDDEN(self);
    Value ret = OBJ_VAL(dupString(f->name));
    return ret;
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
        throwErrorFmt(sysErrClass(err), "Error during file unlink: %s", strerror(err));
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
        throwErrorFmt(sysErrClass(err), "Error during file rename: %s", strerror(err));
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
        throwErrorFmt(sysErrClass(err), "Error during file seek: %s", strerror(err));
    }
    return NUMBER_VAL(pos);
}

static Value lxFileRewind(int argCount, Value *args) {
    CHECK_ARITY("File#rewind", 1, 1, argCount);
    Value seekArgs[2] = {
        NUMBER_VAL(0),
        NUMBER_VAL(SEEK_SET)
    };
    return callMethod(AS_OBJ(*args), INTERN("seek"), 2, seekArgs, NULL);
}

static Value lxFileMode(int argCount, Value *args) {
    CHECK_ARITY("File#mode", 1, 1, argCount);
    LxFile *lxfile = FILE_GETHIDDEN(args[0]);
    struct stat st;
    releaseGVL(THREAD_STOPPED);
    int res = stat(lxfile->name->chars, &st);
    acquireGVL();
    if (res != 0) {
      throwErrorFmt(sysErrClass(errno), "Error during stat for file %s: %s", lxfile->name->chars, strerror(errno));
    }
    return NUMBER_VAL((int)st.st_mode);
}

static Value lxFileUid(int argCount, Value *args) {
    CHECK_ARITY("File#uid", 1, 1, argCount);
    LxFile *lxfile = FILE_GETHIDDEN(args[0]);
    struct stat st;
    releaseGVL(THREAD_STOPPED);
    int res = stat(lxfile->name->chars, &st);
    acquireGVL();
    if (res != 0) {
      throwErrorFmt(sysErrClass(errno), "Error during stat for file %s: %s", lxfile->name->chars, strerror(errno));
    }
    return NUMBER_VAL((int)st.st_uid);
}

static Value lxFileGid(int argCount, Value *args) {
    CHECK_ARITY("File#gid", 1, 1, argCount);
    LxFile *lxfile = FILE_GETHIDDEN(args[0]);
    struct stat st;
    releaseGVL(THREAD_STOPPED);
    int res = stat(lxfile->name->chars, &st);
    acquireGVL();
    if (res != 0) {
      throwErrorFmt(sysErrClass(errno), "Error during stat for file %s: %s", lxfile->name->chars, strerror(errno));
    }
    return NUMBER_VAL((int)st.st_gid);
}


static Value lxFileChmod(int argCount, Value *args) {
    CHECK_ARITY("File#chmod", 2, 2, argCount);
    Value modeVal = args[1];
    CHECK_ARG_BUILTIN_TYPE(modeVal, IS_NUMBER_FUNC, "number", 1);
    int mode = AS_NUMBER(modeVal);
    LxFile *lxfile = FILE_GETHIDDEN(args[0]);
    releaseGVL(THREAD_STOPPED);
    int res = chmod(lxfile->name->chars, mode);
    acquireGVL();
    if (res != 0) {
        throwErrorFmt(sysErrClass(errno), "Error during chmod for file '%s': %s", lxfile->name->chars, strerror(errno));
    }
    lxfile->mode = mode;
    return TRUE_VAL;
}

static Value lxFileChown(int argCount, Value *args) {
    CHECK_ARITY("File#chown", 4, 4, argCount);
    Value pathVal = args[1];
    Value ownerVal = args[2];
    Value groupVal = args[3];
    CHECK_ARG_IS_A(pathVal, lxStringClass, 1);
    CHECK_ARG_BUILTIN_TYPE(ownerVal, IS_NUMBER_FUNC, "number", 2);
    CHECK_ARG_BUILTIN_TYPE(groupVal, IS_NUMBER_FUNC, "number", 3);
    int owner = AS_NUMBER(ownerVal);
    int group = AS_NUMBER(groupVal);
    LxFile *lxfile = FILE_GETHIDDEN(args[0]);
    releaseGVL(THREAD_STOPPED);
    int res = chown(lxfile->name->chars, (uid_t)owner, (gid_t)group);
    acquireGVL();
    if (res != 0) {
        throwErrorFmt(sysErrClass(errno), "Error during chown for file '%s': %s", lxfile->name->chars, strerror(errno));
    }
    return TRUE_VAL;
}

static Value lxFilePasswdInit(int argCount, Value *args) {
    CHECK_ARITY("FilePasswd#init", 2, 2, argCount);
    Value self = args[0];
    Value initArg = args[1];
    if (!IS_NUMBER(initArg) && !IS_A_STRING(initArg)) {
      throwErrorFmt(lxArgErrClass, "FilePasswd must be initialized with a string or number");
    }
    struct passwd *pw = NULL;
    if (IS_NUMBER(initArg)) {
      pw = getpwuid((uid_t)AS_NUMBER(initArg));
    } else {
      pw = getpwnam(AS_CSTRING(initArg));
    }
    if (pw == NULL) {
      throwErrorFmt(sysErrClass(errno), "user info (passwd) retrieval error: %s", strerror(errno));
    }
    Value pw_nameVal = OBJ_VAL(copyString(pw->pw_name, strlen(pw->pw_name), NEWOBJ_FLAG_NONE));
    Value pw_passwdVal = OBJ_VAL(copyString(pw->pw_passwd, strlen(pw->pw_passwd), NEWOBJ_FLAG_NONE));
    Value pw_dirVal = OBJ_VAL(copyString(pw->pw_dir, strlen(pw->pw_dir), NEWOBJ_FLAG_NONE));
    Value pw_shellVal = OBJ_VAL(copyString(pw->pw_shell, strlen(pw->pw_shell), NEWOBJ_FLAG_NONE));
    propertySet(AS_INSTANCE(self), INTERN("name"), pw_nameVal);
    propertySet(AS_INSTANCE(self), INTERN("passwd"), pw_passwdVal);
    propertySet(AS_INSTANCE(self), INTERN("uid"), NUMBER_VAL((int)pw->pw_uid));
    propertySet(AS_INSTANCE(self), INTERN("gid"), NUMBER_VAL((int)pw->pw_gid));
    propertySet(AS_INSTANCE(self), INTERN("dir"), pw_dirVal);
    propertySet(AS_INSTANCE(self), INTERN("shell"), pw_shellVal);
    return self;
}

static Value lxFileGroupInit(int argCount, Value *args) {
    CHECK_ARITY("FileGroup#init", 2, 2, argCount);
    Value self = args[0];
    Value initArg = args[1];
    if (!IS_NUMBER(initArg) && !IS_A_STRING(initArg)) {
      throwErrorFmt(lxArgErrClass, "FileGroup must be initialized with a string or number");
    }
    struct group *gr = NULL;
    if (IS_NUMBER(initArg)) {
      gr = getgrgid((gid_t)AS_NUMBER(initArg));
    } else {
      gr = getgrnam(AS_CSTRING(initArg));
    }
    if (gr == NULL) {
      throwErrorFmt(sysErrClass(errno), "group info retrieval error: %s", strerror(errno));
    }
    Value gr_nameVal = OBJ_VAL(copyString(gr->gr_name, strlen(gr->gr_name), NEWOBJ_FLAG_NONE));
    Value gr_passwdVal = OBJ_VAL(copyString(gr->gr_passwd, strlen(gr->gr_passwd), NEWOBJ_FLAG_NONE));
    propertySet(AS_INSTANCE(self), INTERN("name"), gr_nameVal);
    propertySet(AS_INSTANCE(self), INTERN("passwd"), gr_passwdVal);
    propertySet(AS_INSTANCE(self), INTERN("gid"), NUMBER_VAL((int)gr->gr_gid));
    // TODO: add members array (gr->gr_mem)
    return self;
}

static Value lxFileStatInit(int argCount, Value *args) {
    CHECK_ARITY("FileStat#init", 2, 2, argCount);
    Value self = args[0];
    Value path = args[1];
    if (!IS_A_STRING(path)) {
      throwErrorFmt(lxArgErrClass, "FileStat must be initialized with a string");
    }

    struct stat st;
    releaseGVL(THREAD_STOPPED);
    int res = stat(AS_CSTRING(path), &st);
    acquireGVL();
    if (res != 0) {
      throwErrorFmt(sysErrClass(errno), "stat retrieval error for file %s: %s", AS_CSTRING(path), strerror(errno));
    }
    Value modeVal = NUMBER_VAL((int)st.st_mode);
    Value uidVal = NUMBER_VAL((int)st.st_uid);
    Value gidVal = NUMBER_VAL((int)st.st_gid);
    Value sizeVal = NUMBER_VAL((int)st.st_size);
    propertySet(AS_INSTANCE(self), INTERN("mode"), modeVal);
    propertySet(AS_INSTANCE(self), INTERN("uid"), uidVal);
    propertySet(AS_INSTANCE(self), INTERN("gid"), gidVal);
    propertySet(AS_INSTANCE(self), INTERN("size"), sizeVal);
    return self;
}

void Init_FileClass(void) {
    ObjClass *fileClass = addGlobalClass("File", lxIOClass);
    ObjClass *fileStatic = classSingletonClass(fileClass);

    addNativeMethod(fileStatic, "create", lxFileCreateStatic);
    addNativeMethod(fileStatic, "open", lxFileOpenStatic);
    addNativeMethod(fileStatic, "exists", lxFileExistsStatic);
    addNativeMethod(fileStatic, "read", lxFileReadStatic);
    addNativeMethod(fileStatic, "readLines", lxFileReadLinesStatic);
    addNativeMethod(fileStatic, "user", lxFileUserStatic);
    addNativeMethod(fileStatic, "group", lxFileGroupStatic);
    addNativeMethod(fileStatic, "stat", lxFileStatStatic);
    addNativeMethod(fileStatic, "isDir", lxFileStaticIsDir);
    addNativeMethod(fileStatic, "copy", lxFileStaticCopy);
    addNativeMethod(fileStatic, "symlink", lxFileStaticSymlink);

    addNativeMethod(fileClass, "init", lxFileInit);
    addNativeMethod(fileClass, "write", lxFileWrite);
    addNativeMethod(fileClass, "close", lxFileClose);
    addNativeMethod(fileClass, "path", lxFilePath);
    addNativeMethod(fileClass, "unlink", lxFileUnlink);
    addNativeMethod(fileClass, "rename", lxFileRename);
    addNativeMethod(fileClass, "seek", lxFileSeek);
    addNativeMethod(fileClass, "rewind", lxFileRewind);
    addNativeMethod(fileClass, "chmod", lxFileChmod);
    addNativeMethod(fileClass, "chown", lxFileChown);
    addNativeMethod(fileClass, "mode", lxFileMode);
    addNativeMethod(fileClass, "uid", lxFileUid);
    addNativeMethod(fileClass, "gid", lxFileGid);

    // FIXME: add it under File namespace
    lxFilePasswdClass = addGlobalClass("FilePasswd", lxObjClass);
    addNativeMethod(lxFilePasswdClass, "init", lxFilePasswdInit);

    lxFileGroupClass = addGlobalClass("FileGroup", lxObjClass);
    addNativeMethod(lxFileGroupClass, "init", lxFileGroupInit);

    lxFileStatClass = addGlobalClass("FileStat", lxObjClass);
    addNativeMethod(lxFileStatClass, "init", lxFileStatInit);

    Value fileClassVal = OBJ_VAL(fileClass);
    addConstantUnder("O_RDONLY", NUMBER_VAL(O_RDONLY), fileClassVal);
    addConstantUnder("O_WRONLY", NUMBER_VAL(O_WRONLY), fileClassVal);
    addConstantUnder("O_RDWR", NUMBER_VAL(O_RDWR), fileClassVal);
    addConstantUnder("O_APPEND", NUMBER_VAL(O_APPEND), fileClassVal);
    addConstantUnder("O_CREAT", NUMBER_VAL(O_CREAT), fileClassVal);
    addConstantUnder("O_CLOEXEC", NUMBER_VAL(O_CLOEXEC), fileClassVal);
    addConstantUnder("O_NOFOLLOW", NUMBER_VAL(O_NOFOLLOW), fileClassVal);
#ifndef O_TMPFILE
#define O_TMPFILE 0
#endif
    addConstantUnder("O_TMPFILE", NUMBER_VAL(O_TMPFILE), fileClassVal);
#ifndef O_SYNC
#define O_SYNC 0
#endif
    addConstantUnder("O_SYNC", NUMBER_VAL(O_SYNC), fileClassVal);
    addConstantUnder("O_TRUNC", NUMBER_VAL(O_TRUNC), fileClassVal);
    addConstantUnder("O_EXCL", NUMBER_VAL(O_EXCL), fileClassVal);

    addConstantUnder("SEEK_SET", NUMBER_VAL(SEEK_SET), fileClassVal);
    addConstantUnder("SEEK_CUR", NUMBER_VAL(SEEK_CUR), fileClassVal);
    addConstantUnder("SEEK_END", NUMBER_VAL(SEEK_END), fileClassVal);
#ifndef SEEK_DATA
#define SEEK_DATA 0
#endif
    addConstantUnder("SEEK_DATA", NUMBER_VAL(SEEK_DATA), fileClassVal);
#ifndef SEEK_HOLE
#define SEEK_HOLE 0
#endif
    addConstantUnder("SEEK_HOLE", NUMBER_VAL(SEEK_HOLE), fileClassVal);


    lxFileClass = fileClass;
}
