#include "test.h"
#include "object.h"
#include "vm.h"
#include "memory.h"

static int test_string_object(void) {
    ObjString *string = copyString("", 0);
    T_ASSERT(string);
    pushCString(string, "hi\n", strlen("hi\n"));
    char *cStr = string->chars;
    T_ASSERT_STREQ("hi\n", cStr);
    T_ASSERT_EQ(3, string->length);
cleanup:
    freeObject((Obj*)string, true);
    return 0;
}

static int test_string_pushCStringFmt(void) {
    ObjString *string = copyString("hello", 5);
    T_ASSERT(string);
    pushCStringFmt(string, ", %s", "world");
    char *cStr = string->chars;
    T_ASSERT_STREQ("hello, world", cStr);
    T_ASSERT_EQ(12, string->length);
cleanup:
    freeObject((Obj*)string, true);
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    initVM();
    INIT_TESTS();
    RUN_TEST(test_string_object);
    RUN_TEST(test_string_pushCStringFmt);
    END_TESTS();
}
