#include "test.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

static int test_compile_addition(void) {
    char *src = "1+1;";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_CONSTANT\t" "0000\t" "'1.00'\n"
                     "0002\t" "OP_CONSTANT\t" "0001\t" "'1.00'\n"
                     "0004\t" "OP_ADD\n"
                     "0005\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_compile_global_variable(void) {
    char *src = "var a; a = 1;";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_NIL\n"
                     "0001\t" "OP_DEFINE_GLOBAL\t" "0000\t" "'a'\n"
                     "0003\t" "OP_CONSTANT\t"      "0002\t" "'1.00'\n"
                     "0005\t" "OP_SET_GLOBAL\t"    "0001\t" "'a'\n"
                     "0007\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    initVM();
    INIT_TESTS();
    RUN_TEST(test_compile_addition);
    RUN_TEST(test_compile_global_variable);
    END_TESTS();
}
