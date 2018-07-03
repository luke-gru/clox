#ifndef clox_debugger_h
#define clox_debugger_h

#include "common.h"
#include "vec.h"

typedef struct Breakpoint {
    char *file;
    int line;
} Breakpoint;

typedef struct Debugger {
    bool awaitingPause;
    vec_void_t v_breakpoints;
} Debugger;

void initDebugger(Debugger *dbg);
void freeDebugger(Debugger *dbg); // free internal structures

bool shouldEnterDebugger(Debugger *dbg, char *fname, int curLine, int lastLine);
void enterDebugger(Debugger *dbg);

#endif
