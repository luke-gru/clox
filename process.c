#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"

// module Process, and global process functions

static Value getPid(void) {
    pid_t pid = getpid();
    return NUMBER_VAL(pid);
}

Value lxFork(int argCount, Value *args) {
    CHECK_ARITY("fork", 0, 1, argCount);
    Value func;
    if (argCount == 1) {
        func = *args;
        if (!isCallable(func)) {
            throwArgErrorFmt("Expected argument 1 to be callable, is: %s", typeOfVal(func));
            UNREACHABLE_RETURN(vm.lastErrorThrown);
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

Value lxWaitpid(int argCount, Value *args) {
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

Value lxExec(int argCount, Value *args) {
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

Value lxSystem(int argCount, Value *args) {
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

void Init_ProcessModule(void) {
    ObjModule *processMod = addGlobalModule("Process");
    ObjClass *processModStatic = moduleSingletonClass(processMod);

    addNativeMethod(processModStatic, "pid", lxProcessPidStatic);

    addGlobalFunction("fork", lxFork);
    addGlobalFunction("waitpid", lxWaitpid);
    addGlobalFunction("system", lxSystem);
    addGlobalFunction("exec", lxExec);

    lxProcessMod = processMod;
}
