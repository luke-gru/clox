#include <string.h>
#include "options.h"

CloxOptions options;

char *boolOptNames[] = {
    "debugParser",
    "traceParserCalls",
    NULL,
};

void initOptions(void) {
    if (options._inited) {
        return;
    }
    options.debugParser = false;
    options.traceParserCalls = false;
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
    return 0;
}
