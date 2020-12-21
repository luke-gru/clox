#include <signal.h>
#include <errno.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"

// module Process, and global process functions
ObjModule *lxSignalMod;
SigHandler *sigHandlers = NULL;

typedef struct SignalBuf {
    volatile int cnt[NSIG];
    volatile int size;
} SignalBuf;

SignalBuf sigbuf;

void enqueueSignal(int signo) {
    sigbuf.cnt[signo]++;
    sigbuf.size++;
}

static inline void dequeueSignal(int signo) {
    sigbuf.cnt[signo]--;
    sigbuf.size--;
}

int getSignal() {
    if (sigbuf.size == 0) return -1;
    for (int i = 0; i < NSIG; i++) {
        if (sigbuf.cnt[i] > 0) {
            dequeueSignal(i);
            return i;
        }
    }
    return -1;
}

void execSignal(LxThread *th, int signum) {
    (void)th;
    SigHandler *cur = sigHandlers;
    while (cur) {
        if (cur->signum == signum) {
            (void)callFunctionValue(OBJ_VAL(cur->callable), 0, NULL);
        }
        cur = cur->next;
    }
}

static void sigHandlerFunc(int signum, siginfo_t *sinfo, void *_context) {
    enqueueSignal(signum);
    pthread_mutex_lock(&vm.mainThread->interruptLock);
    SET_TRAP_INTERRUPT(vm.mainThread);
    pthread_mutex_unlock(&vm.mainThread->interruptLock);
    if (vm.mainThread != vm.curThread) {
        threadSchedule(vm.mainThread);
    }
    return;
}

void removeVMSignalHandlers(void) {
    SigHandler *cur = sigHandlers;
    while (cur) {
        signal(cur->signum, SIG_DFL);
        SigHandler *next = cur->next;
        FREE(SigHandler, cur);
        cur = next;
    }

    sigHandlers = NULL;
}

static int addSigHandler(int signum, Value callable) {
    SigHandler *handler = ALLOCATE(SigHandler, 1);
    handler->signum = signum;
    handler->callable = AS_OBJ(callable);
    handler->next = NULL;
    bool processHandlerExistsForSig = false;
    SigHandler *prev = NULL;
    if (sigHandlers == NULL) {
        sigHandlers = handler;
    } else {
        SigHandler *cur = sigHandlers;
        while (cur->next) {
            if (cur->signum == signum) {
                processHandlerExistsForSig = true;
            }
            cur = cur->next;
        }
        cur->next = handler;
        prev = cur;
    }
    if (!processHandlerExistsForSig) {
        struct sigaction sa;
        sa.sa_flags = 0|SA_SIGINFO|SA_RESTART;
        sigset_t blockMask;
        sigemptyset(&blockMask);
        sa.sa_mask = blockMask;
        sa.sa_sigaction = sigHandlerFunc;
        struct sigaction saOld;
        int res = sigaction(signum, &sa, &saOld);
        if (res != 0) {
            prev->next = NULL;
            FREE(SigHandler, handler);
            return -1;
        }
    }
    return 0;
}

static Value lxSignalTrapStatic(int argCount, Value *args) {
    CHECK_ARITY("Signal.trap", 3, 3, argCount);
    Value self = args[0];
    Value signo = args[1];
    Value callable = args[2];
    CHECK_ARG_BUILTIN_TYPE(signo, IS_NUMBER_FUNC, "number", 1);
    if (!isCallable(callable)) {
        throwErrorFmt(lxArgErrClass, "Argument 2 must be a callable");
    }
    int signum = AS_NUMBER(signo);

    OBJ_WRITE(self, callable);
    GC_OLD(AS_OBJ(callable));
    int res = addSigHandler(signum, callable);
    if (res != 0) {
        throwErrorFmt(lxErrClass, "Error adding signal handler: %s", strerror(errno));
    }

    return NIL_VAL;
}

void Init_SignalModule(void) {
    ObjModule *signalMod = addGlobalModule("Signal");
    ObjClass *signalModStatic = moduleSingletonClass(signalMod);

    addNativeMethod(signalModStatic, "trap", lxSignalTrapStatic);

    lxSignalMod = signalMod;

    Value signalModVal = OBJ_VAL(signalMod);
    // can't trap these:
    addConstantUnder("STOP", NUMBER_VAL(SIGSTOP), signalModVal);
    addConstantUnder("KILL", NUMBER_VAL(SIGKILL), signalModVal);

    // $ man 7 signal
    addConstantUnder("HUP",  NUMBER_VAL(SIGHUP), signalModVal);
    addConstantUnder("INT",  NUMBER_VAL(SIGINT), signalModVal);
    addConstantUnder("QUIT", NUMBER_VAL(SIGQUIT), signalModVal);
    addConstantUnder("ILL",  NUMBER_VAL(SIGILL), signalModVal);
    addConstantUnder("ABRT", NUMBER_VAL(SIGABRT), signalModVal);
    addConstantUnder("FPE",  NUMBER_VAL(SIGFPE), signalModVal);
    addConstantUnder("SEGV", NUMBER_VAL(SIGSEGV), signalModVal);
    addConstantUnder("PIPE", NUMBER_VAL(SIGPIPE), signalModVal);

    addConstantUnder("ALRM", NUMBER_VAL(SIGALRM), signalModVal);
    addConstantUnder("TERM", NUMBER_VAL(SIGTERM), signalModVal);
    addConstantUnder("USR1", NUMBER_VAL(SIGUSR1), signalModVal);
    addConstantUnder("USR2", NUMBER_VAL(SIGUSR2), signalModVal);
    addConstantUnder("CHLD", NUMBER_VAL(SIGCHLD), signalModVal);
    addConstantUnder("CONT", NUMBER_VAL(SIGCONT), signalModVal);
    addConstantUnder("TSTP", NUMBER_VAL(SIGTSTP), signalModVal);
    addConstantUnder("TTIN", NUMBER_VAL(SIGTTIN), signalModVal);
    addConstantUnder("TTOU", NUMBER_VAL(SIGTTOU), signalModVal);

#ifdef SIGBUS
    addConstantUnder("BUS", NUMBER_VAL(SIGBUS), signalModVal);
#else
    addConstantUnder("BUS", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGPOLL
    addConstantUnder("POLL", NUMBER_VAL(SIGPOLL), signalModVal);
#else
    addConstantUnder("POLL", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGPROF
    addConstantUnder("PROF", NUMBER_VAL(SIGPROF), signalModVal);
#else
    addConstantUnder("PROF", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGSYS
    addConstantUnder("SYS", NUMBER_VAL(SIGSYS), signalModVal);
#else
    addConstantUnder("SYS", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGTRAP
    addConstantUnder("TRAP", NUMBER_VAL(SIGTRAP), signalModVal);
#else
    addConstantUnder("TRAP", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGURG
    addConstantUnder("URG", NUMBER_VAL(SIGURG), signalModVal);
#else
    addConstantUnder("URG", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGVTALRM
    addConstantUnder("VTALRM", NUMBER_VAL(SIGVTALRM), signalModVal);
#else
    addConstantUnder("VTALRM", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGXCPU
    addConstantUnder("XCPU", NUMBER_VAL(SIGXCPU), signalModVal);
#else
    addConstantUnder("XCPU", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGXFSZ
    addConstantUnder("XFSZ", NUMBER_VAL(SIGXFSZ), signalModVal);
#else
    addConstantUnder("XFSZ", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGIOT
    addConstantUnder("IOT", NUMBER_VAL(SIGIOT), signalModVal);
#else
    addConstantUnder("IOT", NUMBER_VAL(SIGABRT), signalModVal);
#endif

#ifdef SIGEMT
    addConstantUnder("EMT", NUMBER_VAL(SIGEMT), signalModVal);
#else
    addConstantUnder("EMT", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGSTKFLT
    addConstantUnder("STKFLT", NUMBER_VAL(SIGSTKFLT), signalModVal);
#else
    addConstantUnder("STKFLT", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGIO
    addConstantUnder("IO", NUMBER_VAL(SIGIO), signalModVal);
#else
    addConstantUnder("IO", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGCLD
    addConstantUnder("CLD", NUMBER_VAL(SIGCLD), signalModVal);
#else
    addConstantUnder("CLD", NUMBER_VAL(SIGCHLD), signalModVal);
#endif

#ifdef SIGPWR
    addConstantUnder("PWR", NUMBER_VAL(SIGPWR), signalModVal);
#else
    addConstantUnder("PWR", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGINFO
    addConstantUnder("INFO", NUMBER_VAL(SIGINFO), signalModVal);
#else
    addConstantUnder("INFO", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGLOST
    addConstantUnder("LOST", NUMBER_VAL(SIGLOST), signalModVal);
#else
    addConstantUnder("LOST", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGWINCH
    addConstantUnder("WINCH", NUMBER_VAL(SIGWINCH), signalModVal);
#else
    addConstantUnder("WINCH", NUMBER_VAL(0), signalModVal);
#endif

#ifdef SIGUNUSED
    addConstantUnder("UNUSED", NUMBER_VAL(SIGUNUSED), signalModVal);
#else
    addConstantUnder("UNUSED", NUMBER_VAL(0), signalModVal);
#endif

}
