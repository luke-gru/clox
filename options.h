#ifndef clox_scanner_h
#define clox_scanner_h

#include <stdbool.h>

typedef struct sCloxOptions {
    bool debugParser; // default false
} CloxOptions;

void initOptions(void);
CloxOptions *getOptions(void);

#define CLOX_OPTION_T(opt) ((bool)(getOptions()->opt) == true)
#define SET_OPTION(name, val) do {\
    (getOptions())->name = val;\
    } while (0)
#endif
