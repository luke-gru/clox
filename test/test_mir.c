#include "test.h"
#include "debug.h"
#include "mir.h"
#include "vm.h"
#include "compiler.h"

// no optimizations
static Chunk *compNoOpt(char *src, CompileErr *err) {
    bool oldNoOpt = compilerOpts.noOptimize;
    compilerOpts.noOptimize = true;
    Chunk *ret = compile_src(src, err);
    compilerOpts.noOptimize = oldNoOpt;
    return ret;
}

static int test_mir_compiles(void) {
    char *src = "{ var a = 1; print a; }";
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compNoOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    Iseq *iseq = chunk->iseq;
    ASSERT(iseq);
    Mir mir = genMir(iseq);
    emitMir(mir);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    initCoreSighandlers();

    initVM();
    INIT_TESTS();
    RUN_TEST(test_mir_compiles);

    freeVM();
    END_TESTS();
}
