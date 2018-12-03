#include <string.h>
#include "options.h"
#include "debug.h"
#include "compiler.h"
#include "nodes.h"

static CloxOptions options;

char *boolOptNames[] = {
    "printAST",
    "traceParserCalls",
    "traceVMExecution",
    "debugVM",
    "debugTokens",
    "debugBytecode",
    "traceGC",
    "traceCompiler",
    "disableBcodeOptimizer",
    "disableGC",
    "parseOnly",
    "compileOnly",
    NULL,
};

char *stringOptNames[] = {
    "initialLoadPath",
    "initialScript",
    NULL,
};

void initOptions(void) {
    if (options._inited) {
        return;
    }
    options.printAST = false;
    options.debugTokens = false;
    options.debugBytecode = false;
    options.debugVM = false;

    options.traceParserCalls = false;
    options.traceVMExecution = false;
    options.traceGC = false;
    options.traceCompiler = false;

    options.parseOnly = false;
    options.compileOnly = false;
    options.disableBcodeOptimizer = false;

    options.disableGC = false;
    options.stressGC = false;

    options.initialLoadPath = "";
    options.initialScript = "";

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
    } else {
        UNREACHABLE("%s", "invalid option type: '%s'", typeName);
    }
    return false;
}

static void enableAllTraceOptions(void) {
    SET_OPTION(traceParserCalls, true);
    SET_OPTION(traceVMExecution, true);
    SET_OPTION(traceGC, true);
    SET_OPTION(traceCompiler, true);
}

// Assumes *argv is not NULL. Returns the amount to increment
// idx by in the caller's code.
int parseOption(char **argv, int i) {
    initOptions();
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
    if (strcmp(argv[i], "-DTRACE_GC") == 0) {
        SET_OPTION(traceGC, true);
        return 1;
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
    if (strcmp(argv[i], "--disable-bopt") == 0) {
        SET_OPTION(disableBcodeOptimizer, true);
        compilerOpts.noOptimize = true;
        return 1;
    }
    if (strcmp(argv[i], "--disable-GC") == 0) {
        SET_OPTION(disableGC, true);
        return 1;
    }
    if (strcmp(argv[i], "--stress-GC") == 0) {
        SET_OPTION(stressGC, true);
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
