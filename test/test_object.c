#include "test.h"
#include "object.h"
#include "vm.h"
#include "memory.h"

static int test_string_object(void) {
    ObjString *string = newString("", 0);
    T_ASSERT(string);
    pushCString(string, "hi\n", strlen("hi\n"));
    char *cStr = string->chars;
    T_ASSERT_STREQ("hi\n", cStr);
cleanup:
    freeObject((Obj*)string, true);
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    initVM();
    INIT_TESTS();
    RUN_TEST(test_string_object);
    END_TESTS();
}
