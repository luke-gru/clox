#include "test.h"
#include "chunk.h"
#include "value.h"
#include "compiler.h"
#include "vm.h"

static int test_serialize_chunk(void) {
    FILE *f = NULL;
    char *src = "print 1+1; fun myfunc() { return \"WOW\"; } print myfunc();";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    fprintf(stderr, "output: \n'%s'\n", cstring);
    T_ASSERT(strlen(cstring) > 1);

    f = fopen("chunk.loxdat", "wb");
    ASSERT(f);
    int serErr = 0;
    int serRes = serializeChunk(&chunk, f, &serErr);
    fclose(f);
    T_ASSERT_EQ(0, serRes);
    T_ASSERT_EQ(0, serErr);

    f = fopen("chunk.loxdat", "rb");
    ASSERT(f);
    Chunk newChunk;
    initChunk(&newChunk);
    int loadRes = loadChunk(&newChunk, f, &serErr);
    T_ASSERT_EQ(0, loadRes);
    T_ASSERT_EQ(0, serErr);
    T_ASSERT_EQ(chunk.count, newChunk.count);

    ObjString *string2 = disassembleChunk(&newChunk);
    char *cstring2 = string->chars;
    T_ASSERT(strcmp(cstring, cstring2) == 0);

    /*InterpretResult ires = interpret(&newChunk);*/
    /*T_ASSERT_EQ(INTERPRET_OK, ires);*/

cleanup:
    if (f) fclose(f);
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    initVM();
    INIT_TESTS();
    RUN_TEST(test_serialize_chunk);
    END_TESTS();
}
