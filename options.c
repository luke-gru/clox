#include <string.h>
#include "options.h"
#include "debug.h"

static CloxOptions options;

char *boolOptNames[] = {
    "debugParser",
    "traceParserCalls",
    "traceVMExecution",
    "parseOnly",
    "compileOnly",
    "debugTokens",
    "debugBytecode",
    "traceGC",
    "traceCompiler",
    NULL,
};

void initOptions(void) {
    if (options._inited) {
        return;
    }
    options.debugParser = false;
    options.traceParserCalls = false;
    options.traceVMExecution = false;
    options.parseOnly = false;
    options.compileOnly = false;
    options.debugTokens = false;
    options.debugBytecode = false;
    options.traceGC = false;
    options.traceCompiler = false;
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
    } else {
        UNREACHABLE("%s", "invalid option type: '%s'", typeName);
    }
    return false;
}

// Assumes *argv is not NULL. Returns the amount to increment
// idx by in the caller's code.
int parseOption(char **argv, int i) {
    initOptions();
    if (strcmp(argv[i], "-DDEBUG_PARSER") == 0) {
        SET_OPTION(debugParser, true);
        return 1;
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
    if (strcmp(argv[i], "--compile-only") == 0) {
        SET_OPTION(compileOnly, true);
        return 1;
    }
    if (strcmp(argv[i], "--parse-only") == 0) {
        SET_OPTION(parseOnly, true);
        return 1;
    }
    if (strcmp(argv[i], "--debug-tokens") == 0) {
        SET_OPTION(debugTokens, true);
        return 1;
    }
    if (strcmp(argv[i], "--debug-bytecode") == 0) {
        SET_OPTION(debugBytecode, true);
        return 1;
    }
    return 0;
}
