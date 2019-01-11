#include <signal.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"

// module Process, and global process functions
ObjModule *lxSignalMod;

static Value lxSignalTrapStatic(int argCount, Value *args) {
    return *args;
}

void Init_SignalModule(void) {
    ObjModule *signalMod = addGlobalModule("Signal");
    ObjClass *signalModStatic = moduleSingletonClass(signalMod);

    addNativeMethod(signalModStatic, "trap", lxSignalTrapStatic);

    lxSignalMod = signalMod;
}
