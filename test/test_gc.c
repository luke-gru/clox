#include "test.h"
#include "object.h"
#include "vm.h"
#include "memory.h"
#include "debug.h"

static bool isLinkedObject(Obj *obj) {
    ASSERT(vm.inited);
    Obj *linked = vm.objects;
    bool isLinked = false;
    while (linked && !(isLinked = (linked == obj))) {
        linked = linked->next;
    }
    return isLinked;
}

static void fullGC(void) {
    // NOTE: called twice because new objects are created pre-marked,
    // and so are uncollected during first GC after they're created.
    collectGarbage();
    collectGarbage();
}

int test_string_collected(void) {
    initVM();
    ObjString *string = newString("", 0);
    T_ASSERT(isLinkedObject((Obj*)string));
    fullGC();
    T_ASSERT_EQ(false, isLinkedObject((Obj*)string));
cleanup:
    freeVM();
    return 0;
}

int test_hiding_keeps_gc_from_reclaiming(void) {
    initVM();
    ObjString *string = newString("hidden", 6);
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
    INIT_TESTS();
    RUN_TEST(test_string_collected);
    RUN_TEST(test_hiding_keeps_gc_from_reclaiming);
    END_TESTS();
}
