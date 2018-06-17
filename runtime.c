#include <time.h>
#include "runtime.h"

Value runtimeNativeClock(int argCount, Value *args) {
    CHECK_ARGS("clock", 0, 0, argCount);
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);

}

bool runtimeCheckArgs(int min, int max, int actual) {
    return min >= actual && max <= actual;
}
