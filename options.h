#ifndef clox_options_h
#define clox_options_h

#include <stdbool.h>

typedef struct sCloxOptions {
    bool debugParser; // default false
    bool traceParserCalls;

    bool _inited;
} CloxOptions;

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
