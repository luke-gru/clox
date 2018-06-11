#include "test.h"
#include "object.h"

static int test_true(void) {
    T_ASSERT_EQ(0, 0);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    INIT_TESTS();
    RUN_TEST(test_true);
    END_TESTS();
}
