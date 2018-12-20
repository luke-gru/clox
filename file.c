#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "memory.h"
#include "table.h"

ObjClass *lxFileClass;

static char fileReadBuf[4096];

// Does this file exist and is it readable?
static bool fileReadable(char *fname) {
    struct stat buffer;
    return (stat(fname, &buffer) == 0);
}

LxFile *fileGetHidden(Value file) {
    ObjInstance *inst = AS_INSTANCE(file);
    Value internalVal;
    if (tableGet(&inst->hiddenFields, OBJ_VAL(internedString("f", 1)), &internalVal)) {
        DBG_ASSERT(IS_INTERNAL(internalVal));
        LxFile *f = (LxFile*)internalGetData(AS_INTERNAL(internalVal));
        return f;
    } else {
        return NULL;
    }
}

void fileClose(Value file) {
    LxFile *f = FILE_GETHIDDEN(file);
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

Value lxFileReadStatic(int argCount, Value *args) {
    CHECK_ARITY("File.read", 2, 2, argCount);
    Value fname = args[1];
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    ObjString *fnameStr = VAL_TO_STRING(fname);
    if (!fileReadable(fnameStr->chars)) {
        if (errno == EACCES) {
            throwArgErrorFmt("File '%s' not readable", fnameStr->chars);
        } else {
            throwArgErrorFmt("File '%s' not found", fnameStr->chars);
        }
        UNREACHABLE_RETURN(vm.lastErrorThrown);
    }
    // TODO: release and re-acquire GVL for fopen, or is it fast enough?
    int last = errno;
    FILE *f = fopen(fnameStr->chars, "r");
    if (!f) {
        int err = errno;
        errno = last;
        throwArgErrorFmt("Error opening File '%s': %s", fnameStr->chars, strerror(err));
    }
    ObjString *retBuf = copyString("", 0);
    Value ret = newStringInstance(retBuf);
    size_t nread;
    // TODO: release GVL() and make fileReadBuf per-thread
    while ((nread = fread(fileReadBuf, sizeof(fileReadBuf), 1, f)) > 0) {
        pushCString(retBuf, fileReadBuf, nread);
    }
    int readErr = ferror(f);
    if (readErr != 0) {
        int err = errno;
        errno = last;
        throwArgErrorFmt("Error reading File '%s': %s", fnameStr->chars, strerror(err));
    }
    fclose(f);
    return ret;
}

static void markInternalFile(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxFile *f = (LxFile*)internal->data;
    ASSERT(f && f->name);
    blackenObject((Obj*)f->name);
}

static void freeInternalFile(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxFile *f = (LxFile*)internal->data;
    freeObject((Obj*)f->name, true);
    FREE(LxFile, f);
}

static LxFile *initFile(Value fileVal, ObjString *fname, int fd, int flags) {
    ObjInstance *fileObj = AS_INSTANCE(fileVal);
    ObjInternal *internalObj = newInternalObject(NULL, sizeof(LxFile), markInternalFile, freeInternalFile);
    hideFromGC((Obj*)internalObj);
    LxFile *file = ALLOCATE(LxFile, 1); // GCed by default GC free of internalObject
    file->name = dupString(fname);
    file->fd = fd;
    file->oflags = flags;
    file->isOpen = true;
    internalObj->data = file;
    tableSet(&fileObj->hiddenFields, OBJ_VAL(internedString("f", 1)), OBJ_VAL(internalObj));
    unhideFromGC((Obj*)internalObj);
    return file;
}

Value lxFileInit(int argCount, Value *args) {
    CHECK_ARITY("File#init", 2, 2, argCount);
    Value self = args[0];
    Value fname = args[1];
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    ObjString *fnameStr = VAL_TO_STRING(fname);
    int last = errno;
    FILE *f = fopen(fnameStr->chars, "r+");
    if (!f) {
        int err = errno;
        errno = last;
        throwArgErrorFmt("Error opening File '%s': %s", fnameStr->chars, strerror(err));
    }
    int fd = fileno(f);
    initFile(self, fnameStr, fd, O_RDWR);
    return self;
}

Value lxFileCreateStatic(int argCount, Value *args) {
    CHECK_ARITY("File.create", 2, 4, argCount);
    Value fname = args[1];
    // TODO: support flags and mode arguments
    CHECK_ARG_IS_A(fname, lxStringClass, 1);
    int mode = 0664;
    int flags = O_CREAT|O_EXCL|O_RDWR;
    ObjString *fnameStr = VAL_TO_STRING(fname);
    int last = errno;
    int fd = open(fnameStr->chars, flags, mode);
    if (fd < 0) {
        int err = errno;
        errno = last;
        throwArgErrorFmt("Error creating File '%s': %s", fnameStr->chars, strerror(err));
    }
    Value file = OBJ_VAL(newInstance(lxFileClass));
    initFile(file, fnameStr, fd, flags);
    return file;
}

static void checkFileWritable(LxFile *f) {
    if (!f->isOpen) {
        throwErrorFmt(lxErrClass, "File '%s' is not open", f->name->chars);
    }
    if ((f->oflags & O_RDWR) == 0 && (f->oflags & O_WRONLY) == 0) {
        throwErrorFmt(lxErrClass, "File '%s' is not open for writing", f->name->chars);
    }
}

Value lxFileWrite(int argCount, Value *args) {
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

Value lxFileClose(int argCount, Value *args) {
    CHECK_ARITY("File#close", 1, 1, argCount);
    Value self = args[0];
    fileClose(self);
    return NIL_VAL;
}

// Returns a File object for an opened file
/*Value lxFileOpenStatic(int argCount, Value *args) {*/
    /*CHECK_ARITY("File.open", 2, 2, argCount);*/
    /*Value fname = args[1];*/
    /*CHECK_ARG_IS_A(fname, lxStringClass, 1);*/
    /*ObjString *fnameStr = VAL_TO_STRING(fname);*/
/*}*/

void Init_FileClass(void) {
    ObjClass *fileClass = addGlobalClass("File", lxObjClass);
    ObjClass *fileStatic = classSingletonClass(fileClass);

    addNativeMethod(fileClass, "init", lxFileInit);
    addNativeMethod(fileStatic, "read", lxFileReadStatic);
    addNativeMethod(fileClass, "write", lxFileWrite);
    addNativeMethod(fileStatic, "create", lxFileCreateStatic);
    /*addNativeMethod(fileStatic, "open", lxFileOpenStatic);*/
    addNativeMethod(fileClass, "close", lxFileClose);

    Value fileClassVal = OBJ_VAL(fileClass);
    setProp(fileClassVal, internedString("RDONLY", 6), NUMBER_VAL(O_RDONLY));
    setProp(fileClassVal, internedString("WRONLY", 6), NUMBER_VAL(O_WRONLY));
    setProp(fileClassVal, internedString("RDWR", 4), NUMBER_VAL(O_RDWR));
    setProp(fileClassVal, internedString("APPEND", 6), NUMBER_VAL(O_APPEND));
    setProp(fileClassVal, internedString("CREAT", 5), NUMBER_VAL(O_CREAT));

    lxFileClass = fileClass;
}
