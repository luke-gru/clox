#include "test.h"
#include "compiler.h"
#include "debug.h"

static int test_compile_addition(void) {
    char *src = "1+1;";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    char *expected = "0000\t" "OP_CONSTANT\t" "0000\n"
                     "0002\t" "OP_CONSTANT\t" "0001\n"
                     "0004\t" "OP_ADD\n"
                     "0005\t" "OP_RETURN\n";
    ASSERT(strcmp(cstring, expected) == 0);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    INIT_TESTS();
    RUN_TEST(test_compile_addition);
    END_TESTS();
}
