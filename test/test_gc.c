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

static int test_string_collected(void) {
    initVM();
    ObjString *string = copyString("", 0, NEWOBJ_FLAG_NONE);
    T_ASSERT(isLinkedObject((Obj*)string));
    fullGC();
    T_ASSERT_EQ(false, isLinkedObject((Obj*)string));
cleanup:
    freeVM();
    return 0;
}

static int test_hiding_keeps_gc_from_reclaiming(void) {
    initVM();
    ObjString *string = copyString("hidden", 6, NEWOBJ_FLAG_NONE);
    hideFromGC((Obj*)string);
    T_ASSERT(isLinkedObject((Obj*)string));
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

    INIT_TESTS("test_gc");
    REGISTER_T_ASSERT_ON_FAIL(freeVM);
    RUN_TEST(test_string_collected);
    RUN_TEST(test_hiding_keeps_gc_from_reclaiming);
    END_TESTS();
}
