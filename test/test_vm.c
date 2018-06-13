#include "test.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"

static int test_addition(void) {
    char *src = "1+1;";
    CompileErr cerr = COMPILE_ERR_NONE;
    InterpretResult ires = INTERPRET_OK;

    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &cerr);
    T_ASSERT_EQ(0, result);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);

    initVM();
    ires = interpret(&chunk);
    T_ASSERT_EQ(INTERPRET_OK, ires);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    INIT_TESTS();
    RUN_TEST(test_addition);
    END_TESTS();
}
