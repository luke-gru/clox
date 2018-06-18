#include <time.h>
#include "runtime.h"

Value runtimeNativeClock(int argCount, Value *args) {
    CHECK_ARGS("clock", 0, 0, argCount);
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);

}

Value runtimeNativeTypeof(int argCount, Value *args) {
    CHECK_ARGS("typeof", 1, 1, argCount);
    const char *strType = typeOfVal(*args);
    return OBJ_VAL(copyString(strType, strlen(strType)));
}

bool runtimeCheckArgs(int min, int max, int actual) {
    return min >= actual && max <= actual;
}
