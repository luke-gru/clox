#include "test.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"

static InterpretResult interp(char *src, bool expectSuccess) {
    CompileErr cerr = COMPILE_ERR_NONE;
    InterpretResult ires = INTERPRET_OK;

    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &cerr);
    if (expectSuccess) {
        T_ASSERT_EQ(0, result);
        T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    }
    initVM();
    ires = interpret(&chunk);
    if (expectSuccess) {
        T_ASSERT_EQ(INTERPRET_OK, ires);
    }
    return ires;
cleanup:
    return INTERPRET_RUNTIME_ERROR;
}

static int test_addition(void) {
    char *src = "1+1;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_NUMBER(*val));
    T_ASSERT_EQ(2.0, AS_NUMBER(*val));
cleanup:
    return 0;
}

static int test_subtraction(void) {
    char *src = "1-3;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_NUMBER(*val));
    T_ASSERT_EQ(-2.0, AS_NUMBER(*val));
cleanup:
    return 0;
}

static int test_negation(void) {
    char *src = "---2.0;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_NUMBER(*val));
    T_ASSERT_EQ(-2.0, AS_NUMBER(*val));
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    INIT_TESTS();
    RUN_TEST(test_addition);
    RUN_TEST(test_subtraction);
    RUN_TEST(test_negation);
    END_TESTS();
}
