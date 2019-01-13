#include "test.h"
#include "object.h"
#include "vm.h"
#include "memory.h"
#include "debug.h"

static bool isLinkedObject(Obj *obj) {
    return obj->type != OBJ_T_NONE;
}

static void fullGC(void) {
    // NOTE: called twice because new objects are created pre-marked,
    // and so are uncollected during first GC after they're created.
    collectGarbage();
    collectGarbage();
}

int test_string_collected(void) {
    initVM();
    ObjString *string = copyString("", 0);
    T_ASSERT(isLinkedObject((Obj*)string));
    fullGC();
    T_ASSERT_EQ(false, isLinkedObject((Obj*)string));
cleanup:
    freeVM();
    return 0;
}

int test_hiding_keeps_gc_from_reclaiming(void) {
    initVM();
    ObjString *string = copyString("hidden", 6);
    T_ASSERT(isLinkedObject((Obj*)string));
    hideFromGC((Obj*)string);
    fullGC();
    T_ASSERT(isLinkedObject((Obj*)string));
cleanup:
    unhideFromGC((Obj*)string);
    freeVM();
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    initCoreSighandlers();

    INIT_TESTS();
    REGISTER_T_ASSERT_ON_FAIL(freeVM);
    RUN_TEST(test_string_collected);
    RUN_TEST(test_hiding_keeps_gc_from_reclaiming);
    END_TESTS();
}
