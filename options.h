#ifndef clox_options_h
#define clox_options_h

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sCloxOptions {
    // debug options (default: false)
    bool printAST;
    bool traceParserCalls;
    bool traceVMExecution;
    bool stepVMExecution;
    bool debugThreads;
    bool debugTokens;
    bool debugBytecode;
    int debugVMLvl;
    int debugRegexLvl;
    int debugOptimizerLvl;
    int traceGCLvl;
    bool traceCompiler;
    bool disableBcodeOptimizer;
    bool disableGC;
    bool stressGCYoung;
    bool stressGCFull;
    bool stressGCBoth;

    bool compileOnly;
    bool parseOnly;
    bool profileGC;

    char *initialLoadPath; // COLON-separated load path
    char *initialScript;

    bool _inited; // internal use, if singleton is inited
    bool end; // if hit end of options (--)
    int index; // index into ARGV
} CloxOptions; // [singleton]

extern int origArgc;
extern char **origArgv;

void initOptions(int argc, char **argv);
CloxOptions *getOptions(void);
bool findOption(const char *optName, const char *typeName);

int parseOption(char **argv, int idx);

#define GET_OPTION(opt) (getOptions()->opt)
#define CLOX_OPTION_T(opt) ((bool)(getOptions()->opt) == true)
#define OPTION_T(opt) CLOX_OPTION_T(opt)
#define SET_OPTION(name, val) do {\
    (getOptions())->name = val;\
    } while (0)
#define IS_OPTION(name, type) (findOption(name, #type) == true)

#ifdef __cplusplus
}
#endif

#endif
