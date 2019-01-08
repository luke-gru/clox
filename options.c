#include <string.h>
#include "options.h"
#include "debug.h"
#include "compiler.h"
#include "nodes.h"

static CloxOptions options;
int origArgc = -1;
char **origArgv = NULL;

char *boolOptNames[] = { // order doesn't matter
    "printAST",
    "traceParserCalls",
    "traceVMExecution",
    "stepVMExecution",
    "debugVM",
    "debugThreads",
    "debugTokens",
    "debugBytecode",
    "traceCompiler",
    "disableBcodeOptimizer",
    "disableGC",
    "profileGC",
    "stressGCYoung",
    "stressGCFull",
    "stressGCBoth",
    "parseOnly",
    "compileOnly",
    NULL
};

char *stringOptNames[] = { // order doesn't matter
    "initialLoadPath",
    "initialScript",
    NULL
};

char *intOptNames[] = { // order doesn't matter
    "traceGCLvl",
    NULL
};

void initOptions(int argc, char **argv) {
    if (options._inited) {
        return;
    }
    origArgc = argc;
    origArgv = argv;
    options.printAST = false;
    options.debugTokens = false;
    options.debugBytecode = false;
    options.debugVM = false;
    options.debugThreads = false;

    options.traceParserCalls = false;
    options.traceVMExecution = false;
    options.stepVMExecution = false;
    options.traceCompiler = false;

    options.parseOnly = false;
    options.compileOnly = false;
    options.disableBcodeOptimizer = false;

    options.disableGC = false;
    options.profileGC = false;
    options.stressGCYoung = false;
    options.stressGCFull = false;
    options.stressGCBoth = false;

    options.initialLoadPath = "";
    options.initialScript = "";

    options.traceGCLvl = 0;

    options._inited = true;
}

CloxOptions *getOptions(void) {
    return &options;
}

// ex: findOption("traceParser", "bool");
bool findOption(const char *optName, const char *typeName) {
    if (strcmp(typeName, "bool") == 0) {
        int i = 0;
        while (boolOptNames[i]) {
            if (strcmp(optName, boolOptNames[i]) == 0) {
                return true;
            }
            i++;
        }
        return false;
    } else if (strcmp(typeName, "string") == 0) {
        int i = 0;
        while (stringOptNames[i]) {
            if (strcmp(optName, stringOptNames[i]) == 0) {
                return true;
            }
            i++;
        }
        return false;
    } else if (strcmp(typeName, "int") == 0) {
        int i = 0;
        while (intOptNames[i]) {
            if (strcmp(optName, intOptNames[i]) == 0) {
                return true;
            }
            i++;
        }
        return false;
    } else {
        UNREACHABLE("%s", "invalid option type: '%s'", typeName);
    }
    return false;
}

static void enableAllTraceOptions(void) {
    SET_OPTION(traceParserCalls, true);
    SET_OPTION(traceVMExecution, true);
    SET_OPTION(traceGCLvl, 2);
    SET_OPTION(traceCompiler, true);
}

// Assumes *argv is not NULL. Returns the amount to increment
// idx by in the caller's code.
int parseOption(char **argv, int i) {
    if (strcmp(argv[i], "-L") == 0) {
        if (argv[i+1]) {
            char *path = calloc(strlen(argv[i+1])+2, 1);
            ASSERT_MEM(path);
            strcpy(path, argv[i+1]);
            if (path[strlen(path)-1] != ':') {
                strcat(path, ":");
            }
            SET_OPTION(initialLoadPath, path);
            return 2;
        } else {
            fprintf(stderr, "[WARN]: Load path not given, ignoring. Example: -L $HOME/workspace\n");
            return 1;
        }
    }

    if (strcmp(argv[i], "-f") == 0) {
        if (argv[i+1]) {
            SET_OPTION(initialScript, argv[i+1]);
            return 2;
        } else {
            fprintf(stderr, "[WARN]: Path to script file not given with -f flag\n");
            return 1;
        }
    }

    if (strcmp(argv[i], "-DTRACE_PARSER_CALLS") == 0) {
        SET_OPTION(traceParserCalls, true);
        return 1;
    }
    if (strcmp(argv[i], "-DTRACE_COMPILER") == 0) {
        SET_OPTION(traceCompiler, true);
        return 1;
    }
    if (strcmp(argv[i], "-DTRACE_VM_EXECUTION") == 0) {
        SET_OPTION(traceVMExecution, true);
        return 1;
    }
    if (strcmp(argv[i], "-DSTEP_VM_EXECUTION") == 0) {
        SET_OPTION(stepVMExecution, true);
        SET_OPTION(traceVMExecution, true);
        return 1;
    }
    if (strcmp(argv[i], "-DTRACE_GC_LVL") == 0) {
        if (!argv[i+1]) {
            return 1;
        }
        char *lvlStr = argv[++i];
        int lvl = atoi(lvlStr);
        if (lvl < 0) lvl = 0;
        SET_OPTION(traceGCLvl, lvl);
        return 2;
    }
    if (strcmp(argv[i], "-DTRACE_ALL") == 0) {
        enableAllTraceOptions();
        return 1;
    }
    if (strcmp(argv[i], "--debug-tokens") == 0) {
        SET_OPTION(debugTokens, true);
        return 1;
    }
    if (strcmp(argv[i], "--print-ast") == 0) {
        astDetailLevel++;
        SET_OPTION(printAST, true);
        return 1;
    }
    if (strcmp(argv[i], "--debug-bytecode") == 0) {
        SET_OPTION(debugBytecode, true);
        return 1;
    }
    if (strcmp(argv[i], "--debug-VM") == 0) {
        SET_OPTION(debugVM, true);
        return 1;
    }
    if (strcmp(argv[i], "--debug-threads") == 0) {
        SET_OPTION(debugThreads, true);
        return 1;
    }
    if (strcmp(argv[i], "--disable-bopt") == 0) {
        SET_OPTION(disableBcodeOptimizer, true);
        compilerOpts.noOptimize = true;
        return 1;
    }
    if (strcmp(argv[i], "--disable-GC") == 0) {
        SET_OPTION(disableGC, true);
        return 1;
    }
    if (strcmp(argv[i], "--profile-GC") == 0) {
        SET_OPTION(profileGC, true);
        return 1;
    }
    if (strcmp(argv[i], "--stress-GC=young") == 0) {
        SET_OPTION(stressGCYoung, true);
        return 1;
    }
    if (strcmp(argv[i], "--stress-GC=full") == 0) {
        SET_OPTION(stressGCFull, true);
        return 1;
    }
    if (strcmp(argv[i], "--stress-GC=both") == 0) {
        SET_OPTION(stressGCBoth, true);
        return 1;
    }
    if (strcmp(argv[i], "--stress-GC=none") == 0) {
        return 1;
    }

    if (strcmp(argv[i], "--compile-only") == 0) {
        SET_OPTION(compileOnly, true);
        return 1;
    }
    if (strcmp(argv[i], "--parse-only") == 0) {
        SET_OPTION(parseOnly, true);
        return 1;
    }
    return 0;
}
