#include "test.h"
#include "regex.h"
#include "debug.h"

int test_compile_empty(void) {
    Regex re;
    regex_init(&re, "", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_empty(void) {
    Regex re;
    regex_init(&re, "", NULL);
    (void)regex_compile(&re);
    MatchData mdata = regex_match(&re, "a string");
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(0, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_only_atoms_success(void) {
    Regex re;
    regex_init(&re, "abba", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_only_atoms_success(void) {
    Regex re;
    regex_init(&re, "abba", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    MatchData mdata = regex_match(&re, "00abba00");
    T_ASSERT_EQ(2, mdata.match_start);
    T_ASSERT_EQ(4, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_only_atoms_nomatch(void) {
    Regex re;
    regex_init(&re, "abba", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    T_ASSERT_EQ(false, regex_match(&re, "00abbc00").matched);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_only_atoms_with_alts_success(void) {
    Regex re;
    regex_init(&re, "abb|a", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    MatchData mdata = regex_match(&re, "00abab00");
    T_ASSERT_EQ(2, mdata.match_start);
    T_ASSERT_EQ(3, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_only_atoms_with_2_alts_success(void) {
    Regex re;
    regex_init(&re, "abb|a|c", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    MatchData mdata = regex_match(&re, "abc");
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(3, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_simple_group(void) {
    Regex re;
    regex_init(&re, "(ab)", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_nested_groups(void) {
    Regex re;
    regex_init(&re, "(ab(cd|e))", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_error_unclosed_group(void) {
    Regex re;
    regex_init(&re, "(ab(cd|e)", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_PARSE_ERR, comp_res);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_repeat(void) {
    Regex re;
    regex_init(&re, "ab+", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_repeat2(void) {
    Regex re;
    regex_init(&re, "a+", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    T_ASSERT_EQ(NODE_REPEAT, re.node->children->type);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_repeat_z(void) {
    Regex re;
    regex_init(&re, "ab*", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_repeat_z2(void) {
    Regex re;
    regex_init(&re, "a*", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    T_ASSERT_EQ(NODE_REPEAT_Z, re.node->children->type);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_repeat_group(void) {
    Regex re;
    regex_init(&re, "(ab)*", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_repeat_simple(void) {
    Regex re;
    regex_init(&re, "ab+", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    MatchData mdata = regex_match(&re, "0abc");
    T_ASSERT_EQ(1, mdata.match_start);
    T_ASSERT_EQ(2, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_repeat_z_simple(void) {
    Regex re;
    regex_init(&re, "ab*", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    MatchData mdata = regex_match(&re, "0ac");
    T_ASSERT_EQ(1, mdata.match_start);
    T_ASSERT_EQ(1, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_character_class(void) {
    Regex re;
    regex_init(&re, "[ab]", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_character_class_simple(void) {
    Regex re;
    regex_init(&re, "[ab]", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    MatchData mdata = regex_match(&re, "cca");
    T_ASSERT_EQ(2, mdata.match_start);
    T_ASSERT_EQ(1, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_dot(void) {
    Regex re;
    regex_init(&re, ".", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_dot(void) {
    Regex re;
    regex_init(&re, ".", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    T_ASSERT_EQ(NODE_DOT, re.node->children->type);
    MatchData mdata = regex_match(&re, "bbc");
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(1, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_repeat_n(void) {
    Regex re;
    regex_init(&re, "a.{3}", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    T_ASSERT_EQ(NODE_REPEAT_N, re.node->children->next->type);
cleanup:
    regex_free(&re);
    return 0;
}

int test_compile_repeat_n2(void) {
    Regex re;
    regex_init(&re, ".{3}", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    T_ASSERT_EQ(NODE_REPEAT_N, re.node->children->type);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_repeat_n(void) {
    Regex re;
    regex_init(&re, "a.{3}", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    MatchData mdata = regex_match(&re, "bacbd");
    T_ASSERT_EQ(1, mdata.match_start);
    T_ASSERT_EQ(4, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_escapes(void) {
    Regex re;
    regex_init(&re, "\\s*\\d{3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    MatchData mdata = regex_match(&re, "  \t127.0.0.1");
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(12, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_cclass_ranges(void) {
    Regex re;
    regex_init(&re, "[a-zA-Z]{3}", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    MatchData mdata = regex_match(&re, "0azZa");
    T_ASSERT_EQ(1, mdata.match_start);
    T_ASSERT_EQ(3, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_cclass_hyphen(void) {
    Regex re;
    regex_init(&re, "[_-]", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    MatchData mdata = regex_match(&re, "123-");
    T_ASSERT_EQ(3, mdata.match_start);
    T_ASSERT_EQ(1, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_cclass_close_bracket(void) {
    Regex re;
    regex_init(&re, "[\\]]", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    MatchData mdata = regex_match(&re, "[]");
    T_ASSERT_EQ(1, mdata.match_start);
    T_ASSERT_EQ(1, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int test_match_eclass_in_cclass(void) {
    Regex re;
    regex_init(&re, "[\\d]{2}", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    regex_output_ast(&re);
    MatchData mdata = regex_match(&re, "hell01");
    T_ASSERT_EQ(4, mdata.match_start);
    T_ASSERT_EQ(2, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    initSighandlers();

    INIT_TESTS();
    RUN_TEST(test_compile_empty);
    RUN_TEST(test_match_empty);
    RUN_TEST(test_compile_only_atoms_success);
    RUN_TEST(test_match_only_atoms_success);
    RUN_TEST(test_match_only_atoms_nomatch);
    RUN_TEST(test_match_only_atoms_with_alts_success);
    RUN_TEST(test_match_only_atoms_with_2_alts_success);
    RUN_TEST(test_compile_simple_group);
    RUN_TEST(test_compile_nested_groups);
    RUN_TEST(test_compile_error_unclosed_group);
    RUN_TEST(test_compile_repeat);
    RUN_TEST(test_compile_repeat2);
    RUN_TEST(test_compile_repeat_z);
    RUN_TEST(test_compile_repeat_z2);
    RUN_TEST(test_compile_repeat_group);
    RUN_TEST(test_match_repeat_simple);
    RUN_TEST(test_match_repeat_z_simple);
    RUN_TEST(test_compile_character_class);
    RUN_TEST(test_match_character_class_simple);
    RUN_TEST(test_compile_dot);
    RUN_TEST(test_match_dot);
    RUN_TEST(test_compile_repeat_n);
    RUN_TEST(test_compile_repeat_n2);
    RUN_TEST(test_match_repeat_n);
    RUN_TEST(test_match_escapes);
    RUN_TEST(test_match_cclass_ranges);
    RUN_TEST(test_match_cclass_hyphen);
    RUN_TEST(test_match_cclass_close_bracket);
    RUN_TEST(test_match_eclass_in_cclass);
    END_TESTS();
}
