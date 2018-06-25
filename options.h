#ifndef clox_options_h
#define clox_options_h

#include <stdbool.h>

typedef struct sCloxOptions {
    // debug options (default: false)
    bool printAST;
    bool traceParserCalls;
    bool traceVMExecution;
    bool debugVM;
    bool debugTokens;
    bool debugBytecode;
    bool traceGC;
    bool traceCompiler;
    bool disableBcodeOptimizer;

    bool compileOnly;
    bool parseOnly;
    //bool profileGC; // TODO

    bool _inited; // internal use, if singleton is inited
} CloxOptions; // [singleton]

void initOptions(void);
CloxOptions *getOptions(void);
bool findOption(const char *optName, const char *typeName);

int parseOption(char **argv, int idx);

#define CLOX_OPTION_T(opt) ((bool)(getOptions()->opt) == true)
#define SET_OPTION(name, val) do {\
    (getOptions())->name = val;\
    } while (0)
#define IS_OPTION(name, type) (findOption(name, #type) == true)

#endif
