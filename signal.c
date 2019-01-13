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

void dequeueSignal(int signo) {
    sigbuf.cnt[signo]--;
    sigbuf.size--;
}

int getSignal() {
    if (sigbuf.size == 0) return -1;
    for (int i = 0; i < NSIG; i++) {
        if (sigbuf.cnt[i] > 0) {
            sigbuf.cnt[i]--;
            sigbuf.size--;
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
    Value signo = args[1];
    Value callable = args[2];
    CHECK_ARG_BUILTIN_TYPE(signo, IS_NUMBER_FUNC, "number", 1);
    if (!isCallable(callable)) {
        throwErrorFmt(lxArgErrClass, "Argument 2 must be a callable");
    }
    int signum = AS_NUMBER(signo);

    int last = errno;
    OBJ_WRITE(*args, callable);
    GC_OLD(AS_OBJ(callable));
    int res = addSigHandler(signum, callable);
    if (res != 0) {
        int err = errno;
        errno = last;
        throwErrorFmt(lxErrClass, "Error adding signal handler: %s", strerror(err));
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
    setProp(signalModVal, INTERN("STOP"), NUMBER_VAL(SIGSTOP));
    setProp(signalModVal, INTERN("KILL"), NUMBER_VAL(SIGKILL));

    // $ man 7 signal
    setProp(signalModVal, INTERN("HUP"), NUMBER_VAL(SIGHUP));
    setProp(signalModVal, INTERN("INT"), NUMBER_VAL(SIGINT));
    setProp(signalModVal, INTERN("QUIT"), NUMBER_VAL(SIGQUIT));
    setProp(signalModVal, INTERN("ILL"), NUMBER_VAL(SIGILL));
    setProp(signalModVal, INTERN("ABRT"), NUMBER_VAL(SIGABRT));
    setProp(signalModVal, INTERN("FPE"), NUMBER_VAL(SIGFPE));
    setProp(signalModVal, INTERN("SEGV"), NUMBER_VAL(SIGSEGV));
    setProp(signalModVal, INTERN("PIPE"), NUMBER_VAL(SIGPIPE));

    setProp(signalModVal, INTERN("ALRM"), NUMBER_VAL(SIGALRM));
    setProp(signalModVal, INTERN("TERM"), NUMBER_VAL(SIGTERM));
    setProp(signalModVal, INTERN("USR1"), NUMBER_VAL(SIGUSR1));
    setProp(signalModVal, INTERN("USR2"), NUMBER_VAL(SIGUSR2));
    setProp(signalModVal, INTERN("CHLD"), NUMBER_VAL(SIGCHLD));
    setProp(signalModVal, INTERN("CONT"), NUMBER_VAL(SIGCONT));
    setProp(signalModVal, INTERN("TSTP"), NUMBER_VAL(SIGTSTP));
    setProp(signalModVal, INTERN("TTIN"), NUMBER_VAL(SIGTTIN));
    setProp(signalModVal, INTERN("TTOU"), NUMBER_VAL(SIGTTOU));

#ifdef SIGBUS
    setProp(signalModVal, INTERN("BUS"), NUMBER_VAL(SIGBUS));
#else
    setProp(signalModVal, INTERN("BUS"), NUMBER_VAL(0));
#endif

#ifdef SIGPOLL
    setProp(signalModVal, INTERN("POLL"), NUMBER_VAL(SIGPOLL));
#else
    setProp(signalModVal, INTERN("POLL"), NUMBER_VAL(0));
#endif

#ifdef SIGPROF
    setProp(signalModVal, INTERN("PROF"), NUMBER_VAL(SIGPROF));
#else
    setProp(signalModVal, INTERN("PROF"), NUMBER_VAL(0));
#endif

#ifdef SIGSYS
    setProp(signalModVal, INTERN("SYS"), NUMBER_VAL(SIGSYS));
#else
    setProp(signalModVal, INTERN("SYS"), NUMBER_VAL(0));
#endif

#ifdef SIGTRAP
    setProp(signalModVal, INTERN("TRAP"), NUMBER_VAL(SIGTRAP));
#else
    setProp(signalModVal, INTERN("TRAP"), NUMBER_VAL(0));
#endif

#ifdef SIGURG
    setProp(signalModVal, INTERN("URG"), NUMBER_VAL(SIGURG));
#else
    setProp(signalModVal, INTERN("URG"), NUMBER_VAL(0));
#endif

#ifdef SIGVTALRM
    setProp(signalModVal, INTERN("VTALRM"), NUMBER_VAL(SIGVTALRM));
#else
    setProp(signalModVal, INTERN("VTALRM"), NUMBER_VAL(0));
#endif

#ifdef SIGXCPU
    setProp(signalModVal, INTERN("XCPU"), NUMBER_VAL(SIGXCPU));
#else
    setProp(signalModVal, INTERN("XCPU"), NUMBER_VAL(0));
#endif

#ifdef SIGXFSZ
    setProp(signalModVal, INTERN("XFSZ"), NUMBER_VAL(SIGXFSZ));
#else
    setProp(signalModVal, INTERN("XFSZ"), NUMBER_VAL(0));
#endif

#ifdef SIGIOT
    setProp(signalModVal, INTERN("IOT"), NUMBER_VAL(SIGIOT));
#else
    setProp(signalModVal, INTERN("IOT"), NUMBER_VAL(SIGABRT));
#endif

#ifdef SIGEMT
    setProp(signalModVal, INTERN("EMT"), NUMBER_VAL(SIGEMT));
#else
    setProp(signalModVal, INTERN("EMT"), NUMBER_VAL(0));
#endif

#ifdef SIGSTKFLT
    setProp(signalModVal, INTERN("STKFLT"), NUMBER_VAL(SIGSTKFLT));
#else
    setProp(signalModVal, INTERN("STKFLT"), NUMBER_VAL(0));
#endif

#ifdef SIGIO
    setProp(signalModVal, INTERN("IO"), NUMBER_VAL(SIGIO));
#else
    setProp(signalModVal, INTERN("IO"), NUMBER_VAL(0));
#endif

#ifdef SIGCLD
    setProp(signalModVal, INTERN("CLD"), NUMBER_VAL(SIGCLD));
#else
    setProp(signalModVal, INTERN("CLD"), NUMBER_VAL(SIGCHLD));
#endif

#ifdef SIGPWR
    setProp(signalModVal, INTERN("PWR"), NUMBER_VAL(SIGPWR));
#else
    setProp(signalModVal, INTERN("PWR"), NUMBER_VAL(0));
#endif

#ifdef SIGINFO
    setProp(signalModVal, INTERN("INFO"), NUMBER_VAL(SIGINFO));
#else
    setProp(signalModVal, INTERN("INFO"), NUMBER_VAL(0));
#endif

#ifdef SIGLOST
    setProp(signalModVal, INTERN("LOST"), NUMBER_VAL(SIGLOST));
#else
    setProp(signalModVal, INTERN("LOST"), NUMBER_VAL(0));
#endif

#ifdef SIGWINCH
    setProp(signalModVal, INTERN("WINCH"), NUMBER_VAL(SIGWINCH));
#else
    setProp(signalModVal, INTERN("WINCH"), NUMBER_VAL(0));
#endif

#ifdef SIGUNUSED
    setProp(signalModVal, INTERN("UNUSED"), NUMBER_VAL(SIGUNUSED));
#else
    setProp(signalModVal, INTERN("UNUSED"), NUMBER_VAL(0));
#endif

}
