#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>
#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "memory.h"
#include "table.h"

unsigned randSeed = 1;

Value lxRandom(int argCount, Value *args) {
    CHECK_ARITY("random", 0, 1, argCount);
    long res = random();
    if (argCount == 1) {
        Value maxVal = args[0];
        CHECK_ARG_BUILTIN_TYPE(maxVal, IS_NUMBER_FUNC, "number", 1);
        double max = AS_NUMBER(maxVal);
        if (max < 0) max = -max;
        if (max == 0) {
            return NUMBER_VAL(0);
        } else {
            return NUMBER_VAL(res % (int)max);
        }
    } else {
        return NUMBER_VAL(res);
    }
}

void Init_rand() {
    struct timeval tv;
    int res = gettimeofday(&tv, NULL);
    unsigned seed = randSeed;
    if (res == -1) {
        fprintf(stderr, "gettimeofday() failed in Init_rand(): %s\n", strerror(errno));
        errno = 0;
    } else {
        seed = (unsigned)tv.tv_sec;
    }
    randSeed = seed;
    srandom(randSeed);

    addGlobalFunction("random", lxRandom);
}
