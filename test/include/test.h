#ifndef _CLOX_TEST_H_
#define _CLOX_TEST_H_
#include <stdio.h>
#include <setjmp.h>
#include <stdbool.h>
#include "options.h"

#include "vec.h"

#ifndef CLOX_TEST
#define CLOX_TEST 1
#endif

#ifndef LOG_ERR
#define LOG_ERR(...) (fprintf(stderr, __VA_ARGS__))
#endif

static int assertions_passed;
static int assertions_failed;
static int tests_passed;
static int tests_skipped;
static int tests_failed;
static vec_str_t vtests_failed;

static vec_str_t *vtests_only;
static vec_str_t *vtests_skip;

static jmp_buf jmploc;
bool jmpset = false;

static inline void INIT_TESTS(void) {
    assertions_passed = 0;
    assertions_failed = 0;
    tests_passed = 0;
    tests_skipped = 0;
    tests_failed = 0;
    vec_init(&vtests_failed);
}

static inline void END_TESTS(void) {
    fprintf(stdout, "Assertions passed: %d\n", assertions_passed);
    fprintf(stdout, "Assertions failed: %d\n", assertions_failed);
    fprintf(stdout, "Tests passed:  %d\n", tests_passed);
    fprintf(stdout, "Tests skipped: %d\n", tests_skipped);
    fprintf(stdout, "Tests failed:  %d\n", tests_failed);
    char *failed_testfn;
    int i;
    vec_foreach(&vtests_failed, failed_testfn, i) {
        fprintf(stdout, "  ** Failed: %s **\n", failed_testfn);
    }
    if (tests_failed > 0) {
        exit(1);
    } else {
        exit(0);
    }
}

static inline void TESTS_SET_ONLY(vec_str_t *test_fns) {
    vtests_only = test_fns;
}

static inline void TESTS_SET_SKIP(vec_str_t *test_fns) {
    vtests_skip = test_fns;
}

static inline void FAIL_ASSERT(const char *file, int line, const char *func) {
    LOG_ERR("Assertion failed at %s:%d in %s\n", file, line, func);
    assertions_failed++;
    if (jmpset) {
        longjmp(jmploc, 1);
    }
}

static inline void PASS_ASSERT(void) {
    assertions_passed++;
}

#define RUN_TEST(testfn) _RUN_TEST(testfn, #testfn)
static inline void _RUN_TEST(int (*test_fn)(void), const char *fnname) {
    if (vtests_only != NULL && vtests_only->length > 0) {
        char *only;
        bool only_found = false;
        int i;
        vec_foreach(vtests_only, only, i) {
            if (strcmp(fnname, only) == 0) {
                only_found = true;
                break;
            }
        }
        if (!only_found) {
            tests_skipped++;
            LOG_ERR("-- Skipping %s [cmdline=only] --\n", fnname);
            return;
        }
    }

    if (vtests_skip != NULL && vtests_skip->length > 0) {
        char *skip;
        bool skip_found = false;
        int i;
        vec_foreach(vtests_skip, skip, i) {
            if (strcmp(fnname, skip) == 0) {
                skip_found = true;
                break;
            }
        }
        if (skip_found) {
            tests_skipped++;
            LOG_ERR("-- Skipping %s [cmdline=skip] --\n", fnname);
            return;
        }
    }

    LOG_ERR("-- Running %s --\n", fnname);
    int old_failed = assertions_failed;
    int jmpres = setjmp(jmploc);
    jmpset = true;
    int testres;
    if (jmpres == 0) { // jump was set
        testres = test_fn();
        if (testres == 0 && old_failed == assertions_failed) {
            tests_passed++;
        } else {
            tests_failed++;
        }
    } else if (jmpres > 0) { // assertion failure caused jump
        vec_push(&vtests_failed, (char*)fnname);
        tests_failed++;
    }
    jmpset = false;
}

#define SKIP_TEST(testfn) _SKIP_TEST(#testfn)
static inline void _SKIP_TEST(const char *fnname) {
    LOG_ERR("-- Skipping %s --\n", fnname);
    tests_skipped++;
}

static inline void parseTestOptions(int argc, char *argv[]) {
    int i = 0;
    int incrOpt = 0;
    while (argv[i] != NULL) {
        if ((incrOpt = parseOption(argv, i)) > 0) {
            i+=incrOpt;
        } else {
            i+=1;
        }
    }
}

#define T_ASSERT(expr) ((expr) ? PASS_ASSERT() : FAIL_ASSERT(__FILE__, __LINE__, __func__))
#define T_ASSERT_EQ(expr1,expr2) ((expr1==expr2) ? PASS_ASSERT() : FAIL_ASSERT(__FILE__, __LINE__, __func__))

#endif
