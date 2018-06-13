#include "test.h"
#include "object.h"

static int test_string_object(void) {
    ObjString *string = copyString("", 0);
    T_ASSERT(string);
    pushCString(string, "hi\n", strlen("hi\n"));
    char *cStr = string->chars;
    T_ASSERT(strcmp(cStr, "hi\n") == 0);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    INIT_TESTS();
    RUN_TEST(test_string_object);
    END_TESTS();
}
