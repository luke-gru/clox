#ifndef clox_debugger_h
#define clox_debugger_h

#include "common.h"
#include "vec.h"

typedef struct Breakpoint {
    char *file;
    int line;
} Breakpoint;

typedef struct BreakLvl {
    int ndepth;
    int nwidth;
} BreakLvl;

typedef vec_t(BreakLvl) vec_breaklvl_t;

typedef struct Debugger {
    bool awaitingPause;
    vec_void_t v_breakpoints;
    vec_breaklvl_t v_breaklvls;
} Debugger;

void initDebugger(Debugger *dbg);
void freeDebugger(Debugger *dbg); // free internal structures

bool shouldEnterDebugger(Debugger *dbg, char *fname, int curLine, int lastLine,
    int ndepth, int nwidth);
void enterDebugger(Debugger *dbg, char *fname, int lineno, int ndepth, int nwidth);

#endif
