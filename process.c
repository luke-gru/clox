#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"

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

static Value lxForkStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.fork", 1, 2, argCount);
    Value func = NIL_VAL;
    if (argCount == 2) {
        func = args[1];
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
        if (argCount == 2) {
            callCallable(func, 0, false, NULL);
            stopVM(0);
        }
        return NIL_VAL;
    }
}

static Value lxWaitpidStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.waitpid", 2, 3, argCount);
    Value pidVal = args[1];
    CHECK_ARG_BUILTIN_TYPE(pidVal, IS_NUMBER_FUNC, "number", 1);
    pid_t childpid = (pid_t)AS_NUMBER(pidVal);
    int wstatus;
    int flags = 0;
    if (argCount == 3) {
        CHECK_ARG_BUILTIN_TYPE(args[2], IS_NUMBER_FUNC, "number", 2);
        flags = AS_NUMBER(args[2]);
    }
    releaseGVL(THREAD_STOPPED);
    pid_t wret = waitpid(childpid, &wstatus, flags);
    acquireGVL();
    if (wret == -1) { // error, should throw?
        return NUMBER_VAL(-1);
    }
    return NUMBER_VAL(wstatus);
}

static Value lxWaitallStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.waitall", 1, 1, argCount);
    pid_t pid = -1;
    Value ret = newArray();
    while (pid == -1) {
        int wstatus = 0;
        int flags = 0;
        releaseGVL(THREAD_STOPPED);
        pid = waitpid(pid, &wstatus, flags);
        acquireGVL();
        if (pid == -1) {
            if (errno == ECHILD) {
                break;
            } else {
                throwErrorFmt(sysErrClass(errno), "waitall fail: %s", strerror(errno));
            }
        }
        Value el = newArray();
        arrayPush(el, NUMBER_VAL(pid));
        arrayPush(el, NUMBER_VAL(wstatus));
        arrayPush(ret, el);
    }
    return ret;
}

static Value lxProcessWIFEXITEDStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.WIFEXITED", 2, 2, argCount);
    CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
    int status = (int)AS_NUMBER(args[1]);
    bool exited = WIFEXITED(status);
    return BOOL_VAL(exited);
}

static Value lxProcessWEXITSTATUSStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.WEXITSTATUS", 2, 2, argCount);
    CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
    int status = (int)AS_NUMBER(args[1]);
    int exitStatus = WEXITSTATUS(status);
    return NUMBER_VAL(exitStatus);
}

static Value lxProcessWIFSIGNALEDStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.WIFSIGNALED", 2, 2, argCount);
    CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
    int status = (int)AS_NUMBER(args[1]);
    bool signaled = WIFSIGNALED(status);
    return BOOL_VAL(signaled);
}

static Value lxProcessWTERMSIGStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.WTERMSIG", 2, 2, argCount);
    CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
    int status = (int)AS_NUMBER(args[1]);
    int signo = WTERMSIG(status);
    return NUMBER_VAL(signo);
}

static Value lxExecStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.exec", 2, -1, argCount);

    char const **argv = malloc(sizeof(char*)*(argCount+2)); // XXX: only c99
    ASSERT_MEM(argv);
    memset(argv, 0, sizeof(char*)*(argCount+2));
    for (int i = 1; i < argCount; i++) {
        CHECK_ARG_IS_A(args[i], lxStringClass, i+1);
        ASSERT(argv[i-1] == NULL);
        argv[i-1] = VAL_TO_STRING(args[i])->chars;
    }
    ASSERT(argv[argCount+1] == NULL);
    execvp(argv[0], (char *const *)argv);
    fprintf(stderr, "Error during exec: %s\n", strerror(errno));
    xfree(argv);
    // got here, error execing. TODO: throw error?
    return NUMBER_VAL(-1);
}

/**
 * Runs the given command in a subprocess, waits for it to finish,
 * and returns true if exited successfully from command, otherwise false.
 */
static Value lxSystemStatic(int argCount, Value *args) {
    CHECK_ARITY("Process.system", 2, 2, argCount);
    Value cmd = args[1];
    CHECK_ARG_IS_A(cmd, lxStringClass, 1);

    const char *cmdStr = VAL_TO_STRING(cmd)->chars;
    releaseGVL(THREAD_STOPPED);
    int status = system(cmdStr); // man 3 system
    acquireGVL();
    int exitStatus = WEXITSTATUS(status);
    if (exitStatus != 0) {
        return BOOL_VAL(false);
    }
    return BOOL_VAL(true);
}

static Value lxProcessPidStatic(int argCount, Value *args) {
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
    bool toSelf = (pid == (int)getpid());
    if (toSelf) {
        switch (signo) {
            // synchronous signals
            case SIGSEGV:
            case SIGBUS:
            case SIGKILL:
            case SIGILL:
            case SIGFPE:
            case SIGSTOP: {
                kill((pid_t)pid, signo); // man 2 kill
                break;
            }
            // async signals
            default:
                enqueueSignal(signo);
                threadCheckSignals(vm.mainThread);
                break;
        }
    } else {
        kill((pid_t)pid, signo); // man 2 kill
    }
    return NIL_VAL;
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

void Init_ProcessModule(void) {
    ObjModule *processMod = addGlobalModule("Process");
    ObjClass *processModStatic = moduleSingletonClass(processMod);

    addNativeMethod(processModStatic, "pid", lxProcessPidStatic);
    addNativeMethod(processModStatic, "ppid", lxProcessPpidStatic);
    addNativeMethod(processModStatic, "signal", lxProcessSignalStatic);
    addNativeMethod(processModStatic, "detach", lxProcessDetachStatic);

    addNativeMethod(processModStatic, "fork", lxForkStatic);
    addNativeMethod(processModStatic, "waitpid", lxWaitpidStatic);
    addNativeMethod(processModStatic, "waitall", lxWaitallStatic);
    addNativeMethod(processModStatic, "system", lxSystemStatic);
    addNativeMethod(processModStatic, "exec", lxExecStatic);

    addNativeMethod(processModStatic, "WIFEXITED", lxProcessWIFEXITEDStatic);
    addNativeMethod(processModStatic, "WEXITSTATUS", lxProcessWEXITSTATUSStatic);
    addNativeMethod(processModStatic, "WIFSIGNALED", lxProcessWIFSIGNALEDStatic);
    addNativeMethod(processModStatic, "WTERMSIG", lxProcessWTERMSIGStatic);

    lxProcessMod = processMod;

    addConstantUnder("WNOHANG", NUMBER_VAL(WNOHANG), OBJ_VAL(processMod));
}
