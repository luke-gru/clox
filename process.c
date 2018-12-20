#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"

// TODO: find right header
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// module Process, and global process functions
ObjModule *lxProcessMod;

static Value getPid(void) {
    pid_t pid = getpid();
    return NUMBER_VAL(pid);
}
static Value getPpid(void) {
    pid_t pid = getppid();
    return NUMBER_VAL(pid);
}

static Value lxFork(int argCount, Value *args) {
    CHECK_ARITY("fork", 0, 1, argCount);
    Value func;
    if (argCount == 1) {
        func = *args;
        if (!isCallable(func)) {
            throwArgErrorFmt("Expected argument 1 to be callable, is: %s", typeOfVal(func));
        }
    }
    pid_t pid = fork();
    if (pid < 0) { // error, TODO: should throw?
        return NUMBER_VAL(-1);
    }
    if (pid) { // in parent
        return NUMBER_VAL(pid);
    } else { // in child
        if (argCount == 1) {
            callCallable(func, 0, false, NULL);
            stopVM(0);
        }
        return NIL_VAL;
    }
}

static Value lxWaitpid(int argCount, Value *args) {
    CHECK_ARITY("waitpid", 1, 1, argCount);
    Value pidVal = *args;
    pid_t childpid = (pid_t)AS_NUMBER(pidVal);
    int wstatus;
    // TODO: allow wait flags
    releaseGVL();
    pid_t wret = waitpid(childpid, &wstatus, 0);
    acquireGVL();
    if (wret == -1) { // error, should throw?
        return NUMBER_VAL(-1);
    }
    return pidVal;
}

static Value lxExec(int argCount, Value *args) {
    CHECK_ARITY("exec", 1, -1, argCount);

    char const *argv[argCount+2]; // XXX: only c99
    memset(argv, 0, sizeof(char*)*(argCount+2));
    for (int i = 0; i < argCount; i++) {
        CHECK_ARG_IS_A(args[i], lxStringClass, i+1);
        ASSERT(argv[i] == NULL);
        argv[i] = VAL_TO_STRING(args[i])->chars;
    }
    ASSERT(argv[argCount+1] == NULL);
    execvp(argv[0], (char *const *)argv);
    fprintf(stderr, "Error during exec: %s\n", strerror(errno));
    // got here, error execing. TODO: throw error?
    return NUMBER_VAL(-1);
}

/**
 * Runs the given command in a subprocess, waits for it to finish,
 * and returns true if exited successfully from command, otherwise false.
 */
static Value lxSystem(int argCount, Value *args) {
    CHECK_ARITY("system", 1, 1, argCount);
    Value cmd = *args;
    CHECK_ARG_IS_A(cmd, lxStringClass, 1);

    const char *cmdStr = VAL_TO_STRING(cmd)->chars;
    releaseGVL();
    int status = system(cmdStr); // man 3 system
    acquireGVL();
    int exitStatus = WEXITSTATUS(status);
    if (exitStatus != 0) {
        return BOOL_VAL(false);
    }
    return BOOL_VAL(true);
}

Value lxProcessPidStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.pid", 1, 1, argCount);
    return getPid();
}

static Value lxProcessPpidStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.ppid", 1, 1, argCount);
    return getPpid();
}

static Value lxProcessSignalStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.signal", 3, 3, argCount);
    CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
    CHECK_ARG_BUILTIN_TYPE(args[2], IS_NUMBER_FUNC, "number", 2);
    int pid = (int)AS_NUMBER(args[1]);
    int signo = (int)AS_NUMBER(args[2]);
    if (pid <= 0) {
        throwErrorFmt(lxErrClass, "PID must be positive");
    }
    if (signo < 0) {
        throwErrorFmt(lxErrClass, "signo must be non-negative");
    }
    int res = kill((pid_t)pid, signo); // man 2 kill
    if (res == -1) {
        fprintf(stderr, "Error sending signal: %s\n", strerror(errno));
        return BOOL_VAL(false); // TODO: throw error
    } else {
        return BOOL_VAL(true);
    }
}

static void *reapProcess(void *pidArg) {
    long pid = (long)pidArg;
    int wstatus = 0;
    while (waitpid((pid_t)pid, &wstatus, 0) == 0) {
        // ...
    }
    THREAD_DEBUG(3, "Reaped detached process");
    return NULL;
}

static Value lxProcessDetachStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.signal", 2, 2, argCount);
    CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
    long pid = (long)AS_NUMBER(args[1]);
    if (pid <= 0) {
        throwErrorFmt(lxErrClass, "PID must be positive");
    }
    if (pid == (long)AS_NUMBER(getPid())) {
        throwErrorFmt(lxErrClass, "Can't detach current process");
    }
    pthread_t tnew;
    if (pthread_create(&tnew, NULL, reapProcess, (void*)pid) == 0) {
        return BOOL_VAL(true);
    } else {
        THREAD_DEBUG(3, "Error creating reaper thread for Process.detach");
        throwErrorFmt(lxErrClass, "Error creating process reaper thread");
    }
}

static Value lxProcessPwdStatic(int argCount, Value *args) {
    char buf[PATH_MAX];
    int last = errno;
    char *res = getcwd(buf, PATH_MAX);
    if (res == NULL) {
        int err = errno;
        errno = last;
        throwErrorFmt(lxErrClass, "Cannot retrieve current directory: %s", strerror(err));
    }
    return newStringInstance(copyString(buf, strlen(buf)));
}

void Init_ProcessModule(void) {
    ObjModule *processMod = addGlobalModule("Process");
    ObjClass *processModStatic = moduleSingletonClass(processMod);

    addNativeMethod(processModStatic, "pid", lxProcessPidStatic);
    addNativeMethod(processModStatic, "ppid", lxProcessPpidStatic);
    addNativeMethod(processModStatic, "signal", lxProcessSignalStatic);
    addNativeMethod(processModStatic, "detach", lxProcessDetachStatic);
    addNativeMethod(processModStatic, "pwd", lxProcessPwdStatic);

    addGlobalFunction("fork", lxFork);
    addGlobalFunction("waitpid", lxWaitpid);
    addGlobalFunction("system", lxSystem);
    addGlobalFunction("exec", lxExec);

    lxProcessMod = processMod;
}
