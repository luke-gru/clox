#include "debugger.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "vm.h"

#define DBG_PROMPT " > "
#define LINE_SZ 300

void initDebugger(Debugger *dbg) {
    dbg->awaitingPause = false;
    vec_init(&dbg->v_breakpoints);
}

static void freeBreakpt(Breakpoint *bp) {
    free(bp->file);
    bp->file = NULL;
    free(bp);
}

void freeDebugger(Debugger *dbg) {
    dbg->awaitingPause = false;
    Breakpoint *bp = NULL; int i = 0;
    vec_foreach(&dbg->v_breakpoints, bp, i) {
        freeBreakpt(bp);
    }
    vec_deinit(&dbg->v_breakpoints);
}

static bool breakptIsRegistered(Debugger *dbg, char *file, int line) {
    Breakpoint *bp = NULL; int i = 0;
    vec_foreach(&dbg->v_breakpoints, bp, i) {
        if (strcmp(bp->file, file) == 0 && bp->line == line) {
            return true;
        }
    }
    return false;
}

bool shouldEnterDebugger(Debugger *dbg, char *fname, int line, int lastLine,
        int ndepth, int nwidth) {
    if (dbg->awaitingPause) { // debugger() just called
        return true;
    }
    if (dbg->v_breaklvls.length > 0) {
        BreakLvl breaklvl; int i = 0;
        vec_foreach(&dbg->v_breaklvls, breaklvl, i) {
            if ((breaklvl.ndepth == ndepth || breaklvl.ndepth == -1) &&
                    (breaklvl.nwidth == nwidth || breaklvl.nwidth == -1)) {
                return true;
            }
        }
    }
    if (lastLine == line) {
        return false;
    }
    return breakptIsRegistered(dbg, fname, line);
}

const char *debuggerUsage[] = {
    "continue (c)       Continue running the program",
    "setbr [FILE,]LINE  Set a breakpoint on a line",
    "delbr [FILE,]LINE  Delete a specific breakpoint",
    "next (n)           Step over and stop at next statement",
    "into (i)           Step into and stop at next statement",
    /*"frames             View call frames",*/
    /*"eval (e) EXPR      Evaluate expression",*/
    NULL
};

static void registerBreakpt(Debugger *dbg, char *file, int line) {
    if (breakptIsRegistered(dbg, file, line)) {
            return;
    }
    Breakpoint *bp = calloc(sizeof(*bp), 1);
    ASSERT_MEM(bp);
    bp->file = strdup(file);
    bp->line = line;
    vec_push(&dbg->v_breakpoints, (void*)bp);
}

static void deleteBreakpt(Debugger *dbg, char *file, int line) {
    if (!breakptIsRegistered(dbg, file, line)) {
            return;
    }
    Breakpoint *bp = NULL; int i = 0;
    int foundIdx = -1;
    vec_foreach(&dbg->v_breakpoints, bp, i) {
        if (strcmp(bp->file, file) == 0 && line == line) {
            foundIdx = i;
            break;
        }
    }
    if (foundIdx != -1) {
        freeBreakpt((Breakpoint*)dbg->v_breakpoints.data[foundIdx]);
        vec_splice(&dbg->v_breakpoints, foundIdx, 1);
    }
}

void enterDebugger(Debugger *dbg, char *filename, int lineno, int ndepth, int nwidth) {
    fprintf(stdout, "Entered lox debugger\n");
    dbg->awaitingPause = false;
    vec_clear(&dbg->v_breaklvls);
    char buf[LINE_SZ+1] = { '\0' };
    fprintf(stdout, DBG_PROMPT);
    char *idx = NULL;
    while (fgets(buf, LINE_SZ, stdin)) {
        buf[strlen(buf)-1] = '\0'; // strip newline
        if (strcmp(buf, "help") == 0) {
            const char **usageLine = debuggerUsage;
            for (; *usageLine; usageLine++) {
                fprintf(stdout, "%s\n", *usageLine);
            }
        } else if (strcmp(buf, "c") == 0 || strcmp(buf, "continue") == 0) {
            return;
        } else if ((idx = strstr(buf, "setbr")) != NULL) {
            idx += strlen("setbr");
            errno = 0;
            long lline = strtol(idx, NULL, 10);
            if (errno != 0 || lline <= 0) {
                fprintf(stderr, "Invalid command, should be setbr [FILE],lineno\n");
                goto next;
            }
            registerBreakpt(&vm.debugger, "", (int)lline);
            fprintf(stdout, "Successfully set breakpoint\n");
        } else if ((idx = strstr(buf, "delbr")) != NULL) {
            idx += strlen("delbr");
            errno = 0;
            long lline = strtol(idx, NULL, 10);
            if (errno != 0 || lline <= 0) {
                fprintf(stderr, "Invalid command, should be delbr [FILE],lineno\n");
                goto next;
            }
            deleteBreakpt(&vm.debugger, "", (int)lline);
            fprintf(stdout, "Successfully deleted breakpoint\n");
        } else if (strcmp(buf, "n") == 0 || strcmp(buf, "next") == 0) {
            BreakLvl nextWidth = { .ndepth = ndepth, .nwidth = nwidth+1 };
            vec_push(&dbg->v_breaklvls, nextWidth);
            if (ndepth > 0) {
                BreakLvl nextUp = { .ndepth = ndepth-1, .nwidth = -1 };
                vec_push(&dbg->v_breaklvls, nextUp);
                BreakLvl nextLoop = { .ndepth = ndepth, .nwidth = 0 };
                vec_push(&dbg->v_breaklvls, nextLoop);
            }
            return;
        } else if (strcmp(buf, "i") == 0 || strcmp(buf, "into") == 0) {
            BreakLvl nextIn = { .ndepth = ndepth+1, .nwidth = 0 };
            vec_push(&dbg->v_breaklvls, nextIn);
            BreakLvl nextWidth = { .ndepth = ndepth, .nwidth = nwidth+1 };
            vec_push(&dbg->v_breaklvls, nextWidth);
            if (ndepth > 0) {
                BreakLvl nextLoop = { .ndepth = ndepth, .nwidth = 0 };
                vec_push(&dbg->v_breaklvls, nextLoop);
            }
            return;
        } else {
            fprintf(stderr, "Unrecognized command: '%s'\n", buf);
            fprintf(stderr, "'help' for usage details\n");
        }
next:
        memset(buf, 0, LINE_SZ);
        fprintf(stdout, DBG_PROMPT);
    }
    fprintf(stdout, "Exiting...\n");
    exit(1);
}
