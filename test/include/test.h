#ifndef _CLOX_TEST_H_
#define _CLOX_TEST_H_
#include <stdio.h>
#include <setjmp.h>
#include <stdbool.h>
#include "options.h"
#include "debug.h"

#include "vec.h"

#ifndef CLOX_TEST
#define CLOX_TEST 1
#endif

#ifndef LOG_ERR
#define LOG_ERR(...) (fprintf(stderr, __VA_ARGS__))
#endif

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

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
    fprintf(stdout, "%sAssertions passed: %d\n", KGRN, assertions_passed);
    fprintf(stdout, "%sAssertions failed: %d\n", KRED, assertions_failed);
    fprintf(stdout, "%sTests passed:  %d\n", KGRN, tests_passed);
    fprintf(stdout, "%sTests skipped: %d\n", KYEL, tests_skipped);
    fprintf(stdout, "%sTests failed:  %d\n", KRED, tests_failed);
    char *failed_testfn;
    int i;
    vec_foreach(&vtests_failed, failed_testfn, i) {
        fprintf(stdout, "%s  ** Failed: %s **\n", KRED, failed_testfn);
    }
    fprintf(stdout, "%s", KNRM);
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
    LOG_ERR("%sAssertion failed at %s:%d in %s\n", KRED, file, line, func);
    LOG_ERR("%s", KNRM);
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
    initOptions();
    int i = 0;
    int incrOpt = 0;
    vec_str_t *onlies = calloc(sizeof(vec_str_t), 1);
    ASSERT_MEM(onlies);
    vec_init(onlies);
    vec_str_t *skips = calloc(sizeof(vec_str_t), 1);
    ASSERT_MEM(skips);
    vec_init(skips);
    while (argv[i] != NULL) {
        if (i == 0) { i+= 1; continue; }
        if ((incrOpt = parseOption(argv, i)) > 0) {
            i+=incrOpt;
        } else if (strcmp(argv[i], "--only") == 0) {
            vec_push(onlies, argv[i+1]);
            i += 2;
        } else if (strcmp(argv[i], "--skip") == 0) {
            vec_push(skips, argv[i+1]);
            i += 2;
        } else {
            die("Invalid option\n");
        }
    }
    vtests_only = onlies;
    vtests_skip = skips;
}

static inline bool t_assert_streq(char *str1, char *str2) {
    ASSERT(str1);
    ASSERT(str2);
    if (strcmp(str1, str2) == 0) {
        return true;
    } else {
        fprintf(stderr, "---------\n");
        fprintf(stderr, "Expected: \n'%s'\n", str1);
        fprintf(stderr, "---------\n");
        fprintf(stderr, "Actual:   \n'%s'\n", str2);
        fprintf(stderr, "---------\n");
        return false;
    }
}

static inline bool t_assert_valprinteq(char *expected, Value val) {
    ASSERT(expected);
    ObjString *valOut = valueToString(val, newStackString);
    ASSERT(valOut);
    return t_assert_streq(expected, valOut->chars);
}

#define T_ASSERT(expr) ((expr) ? PASS_ASSERT() : FAIL_ASSERT(__FILE__, __LINE__, __func__))
#define T_ASSERT_EQ(expr1,expr2) ((expr1==expr2) ? PASS_ASSERT() : FAIL_ASSERT(__FILE__, __LINE__, __func__))
#define T_ASSERT_STREQ(str1,str2) (t_assert_streq(str1, str2) ? PASS_ASSERT() : FAIL_ASSERT(__FILE__, __LINE__, __func__))
#define T_ASSERT_VALPRINTEQ(str,value) (t_assert_valprinteq(str, value) ? PASS_ASSERT() : FAIL_ASSERT(__FILE__, __LINE__, __func__))

#endif
