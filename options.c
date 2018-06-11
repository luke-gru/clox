#include "options.h"

CloxOptions options;

void initOptions(void) {
    options.debugParser = false;
}

CloxOptions *getOptions(void) {
    return &options;
}
