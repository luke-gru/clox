#include "test.h"
#include "regex.h"
#include "debug.h"
#include "vm.h"

static int test_compile_empty(void) {
    Regex re;
    regex_init(&re, "", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_empty(void) {
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

static int test_compile_only_atoms_success(void) {
    Regex re;
    regex_init(&re, "abba", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_only_atoms_success(void) {
    Regex re;
    regex_init(&re, "abba", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "00abba00");
    T_ASSERT_EQ(2, mdata.match_start);
    T_ASSERT_EQ(4, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_only_atoms_nomatch(void) {
    Regex re;
    regex_init(&re, "abba", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    T_ASSERT_EQ(false, regex_match(&re, "00abbc00").matched);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_only_atoms_with_alts_success(void) {
    Regex re;
    regex_init(&re, "ab(b|a)", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "00abab00");
    T_ASSERT(mdata.matched);
    T_ASSERT_EQ(2, mdata.match_start);
    T_ASSERT_EQ(3, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_only_atoms_with_alts_no_parens_success(void) {
    Regex re;
    regex_init(&re, "abcd|abce", NULL);
    // = ((|(abcde)(abce)))
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "00abce00");
    T_ASSERT(mdata.matched);
    T_ASSERT_EQ(2, mdata.match_start);
    T_ASSERT_EQ(4, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_only_atoms_with_alts_no_parens_repeat_success(void) {
    Regex re;
    regex_init(&re, "abcd|abce*", NULL);
    // = ((|(abcde)(abce)))
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "00abceeee00");
    T_ASSERT(mdata.matched);
    T_ASSERT_EQ(2, mdata.match_start);
    T_ASSERT_EQ(7, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_only_atoms_with_2_alts_success(void) {
    Regex re;
    regex_init(&re, "ab(b|a|c)", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "abc");
    T_ASSERT(mdata.matched);
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(3, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_simple_group(void) {
    Regex re;
    regex_init(&re, "(ab)", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_nested_groups(void) {
    Regex re;
    regex_init(&re, "(ab(cd|e))", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_error_unclosed_group(void) {
    Regex re;
    regex_init(&re, "(ab(cd|e)", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_PARSE_ERR, comp_res);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_repeat(void) {
    Regex re;
    regex_init(&re, "ab+", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_repeat2(void) {
    Regex re;
    regex_init(&re, "a+", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    T_ASSERT_EQ(NODE_REPEAT, re.node->children->type);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_repeat_z(void) {
    Regex re;
    regex_init(&re, "ab*", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_repeat_z2(void) {
    Regex re;
    regex_init(&re, "a*", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    T_ASSERT_EQ(NODE_REPEAT_Z, re.node->children->type);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_repeat_group(void) {
    Regex re;
    regex_init(&re, "(ab)*", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_repeat_simple(void) {
    Regex re;
    regex_init(&re, "ab+", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "0abc");
    T_ASSERT_EQ(1, mdata.match_start);
    T_ASSERT_EQ(2, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_repeat_z_simple(void) {
    Regex re;
    regex_init(&re, "ab*", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "0ac");
    T_ASSERT_EQ(1, mdata.match_start);
    T_ASSERT_EQ(1, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_character_class(void) {
    Regex re;
    regex_init(&re, "[ab]", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_character_class_simple(void) {
    Regex re;
    regex_init(&re, "[ab]", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "cca");
    T_ASSERT_EQ(2, mdata.match_start);
    T_ASSERT_EQ(1, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_dot(void) {
    Regex re;
    regex_init(&re, ".", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_dot(void) {
    Regex re;
    regex_init(&re, ".", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    T_ASSERT_EQ(NODE_DOT, re.node->children->type);
    MatchData mdata = regex_match(&re, "bbc");
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(1, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_repeat_n(void) {
    Regex re;
    regex_init(&re, "a.{3}", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    T_ASSERT_EQ(NODE_REPEAT_N, re.node->children->next->type);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_repeat_n2(void) {
    Regex re;
    regex_init(&re, ".{3}", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    T_ASSERT_EQ(NODE_REPEAT_N, re.node->children->type);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_repeat_n(void) {
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


static int test_match_escapes(void) {
    Regex re;
    regex_init(&re, "\\s*\\d{3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "  \t127.0.0.1");
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(12, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_cclass_ranges(void) {
    Regex re;
    regex_init(&re, "[a-zA-Z]{3}", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "0azZa");
    T_ASSERT_EQ(1, mdata.match_start);
    T_ASSERT_EQ(3, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_cclass_hyphen(void) {
    Regex re;
    regex_init(&re, "[_-]", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "123-");
    T_ASSERT_EQ(3, mdata.match_start);
    T_ASSERT_EQ(1, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_cclass_close_bracket(void) {
    Regex re;
    regex_init(&re, "[\\]]", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "[]");
    T_ASSERT_EQ(1, mdata.match_start);
    T_ASSERT_EQ(1, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_eclass_in_cclass(void) {
    Regex re;
    regex_init(&re, "[\\d]{2}", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "hell01");
    T_ASSERT_EQ(4, mdata.match_start);
    T_ASSERT_EQ(2, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_line_anchors(void) {
    Regex re;
    regex_init(&re, "^hi$", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_bol_anchor(void) {
    Regex re;
    regex_init(&re, "^hi", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "hi there");
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(2, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_bol_anchor_at_line(void) {
    Regex re;
    regex_init(&re, "^hi", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "l\nhi there");
    T_ASSERT_EQ(2, mdata.match_start);
    T_ASSERT_EQ(2, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_nomatch_bol_anchor(void) {
    Regex re;
    regex_init(&re, "^hi", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "lhi there");
    T_ASSERT_EQ(false, mdata.matched);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_bos_anchor(void) {
    Regex re;
    regex_init(&re, "\\Ahi", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "hi there");
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(2, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_nomatch_bos_anchor(void) {
    Regex re;
    regex_init(&re, "\\Ahi", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "lhi there");
    T_ASSERT_EQ(false, mdata.matched);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_eol_anchor(void) {
    Regex re;
    regex_init(&re, "hi$", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "lolhi");
    T_ASSERT_EQ(3, mdata.match_start);
    T_ASSERT_EQ(2, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_eol_anchor_at_line(void) {
    Regex re;
    regex_init(&re, "hi$", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "lolhi\nother");
    T_ASSERT_EQ(3, mdata.match_start);
    T_ASSERT_EQ(2, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_nomatch_eol_anchor(void) {
    Regex re;
    regex_init(&re, "hi$", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "lolhi5");
    T_ASSERT_EQ(false, mdata.matched);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_match_eos_anchor(void) {
    Regex re;
    regex_init(&re, "hi\\Z", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "lolhi");
    T_ASSERT_EQ(3, mdata.match_start);
    T_ASSERT_EQ(2, mdata.match_len);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_nomatch_eos_anchor(void) {
    Regex re;
    regex_init(&re, "hi\\Z", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    MatchData mdata = regex_match(&re, "lolhi5");
    T_ASSERT_EQ(false, mdata.matched);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_compile_string_anchors(void) {
    Regex re;
    regex_init(&re, "\\Ahi\\Z", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
cleanup:
    regex_free(&re);
    return 0;
}

static int test_capture_groups_nodes(void) {
    Regex re;
    regex_init(&re, "(hi)", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    T_ASSERT_EQ(NODE_GROUP, re.groups->group->type);

    char *str = "hithere";
    MatchData mdata = regex_match(&re, str);
    T_ASSERT(mdata.matched);
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(2, mdata.match_len);
    /*regex_output_ast(&re);*/
    T_ASSERT_EQ(NODE_GROUP, re.node->children->type);
    T_ASSERT_EQ(str, re.node->children->capture_beg);
    T_ASSERT_EQ(str+2, re.node->children->capture_end);
    T_ASSERT_EQ(re.node->children, re.groups->group);

cleanup:
    regex_free(&re);
    return 0;
}

static int test_capture_groups_nodes_with_nonatom(void) {
    Regex re;
    regex_init(&re, "([hi])", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    char *str = "h";
    MatchData mdata = regex_match(&re, str);
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(1, mdata.match_len);
    T_ASSERT_EQ(NODE_GROUP, re.node->children->type);
    T_ASSERT_EQ(str, re.node->children->capture_beg);
    T_ASSERT_EQ(str+1, re.node->children->capture_end);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_capture_groups_nodes_with_nonatom2(void) {
    Regex re;
    regex_init(&re, "GET ([\\w/]+) HTTP", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    char *str = "GET / HTTP/1.1";
    MatchData mdata = regex_match(&re, str);
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(10, mdata.match_len);
    T_ASSERT_EQ(NODE_GROUP, re.groups->group->type);
    T_ASSERT_EQ(str+4, re.groups->group->capture_beg);
    T_ASSERT_EQ(str+5, re.groups->group->capture_end);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_capture_groups_nodes_with_nonatom3(void) {
    Regex re;
    regex_init(&re, "GET ([\\w/.]+) HTTP", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    char *str = "GET /object.c HTTP/1.1";
    MatchData mdata = regex_match(&re, str);
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(18, mdata.match_len);
    T_ASSERT_EQ(NODE_GROUP, re.groups->group->type);
    T_ASSERT_EQ(str+4, re.groups->group->capture_beg);
    T_ASSERT_EQ(str+13, re.groups->group->capture_end);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_repeat_maximal_munch(void) {
    Regex re;
    regex_init(&re, "(.+)hi", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    char *str = "wowhihihi";
    MatchData mdata = regex_match(&re, str);
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(9, mdata.match_len);
    T_ASSERT_EQ(NODE_GROUP, re.groups->group->type);
    T_ASSERT_EQ(str+0, re.groups->group->capture_beg);
    T_ASSERT_EQ(str+7, re.groups->group->capture_end);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_repeat_nomatch_if_next_not_matched(void) {
    Regex re;
    regex_init(&re, "(.+)hi", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    char *str = "wowhello";
    MatchData mdata = regex_match(&re, str);
    T_ASSERT_EQ(false, mdata.matched);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_repeat_nongreedy_minimal_munch(void) {
    Regex re;
    regex_init(&re, "(.+?)hi", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    char *str = "wowhihihi";
    MatchData mdata = regex_match(&re, str);
    T_ASSERT_EQ(true, mdata.matched);
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(5, mdata.match_len);
    T_ASSERT_EQ(NODE_GROUP, re.groups->group->type);
    T_ASSERT_EQ(str+0, re.groups->group->capture_beg);
    T_ASSERT_EQ(str+3, re.groups->group->capture_end);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_repeatz_maximal_munch(void) {
    Regex re;
    regex_init(&re, "(.*)hi", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    char *str = "wowhihihi";
    MatchData mdata = regex_match(&re, str);
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(9, mdata.match_len);
    T_ASSERT_EQ(NODE_GROUP, re.groups->group->type);
    T_ASSERT_EQ(str+0, re.groups->group->capture_beg);
    T_ASSERT_EQ(str+7, re.groups->group->capture_end);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_repeatz_nomatch_if_next_not_matched(void) {
    Regex re;
    regex_init(&re, "(.*)hi", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    char *str = "wowhello";
    MatchData mdata = regex_match(&re, str);
    T_ASSERT_EQ(false, mdata.matched);
cleanup:
    regex_free(&re);
    return 0;
}

static int test_repeatz_nongreedy_minimal_munch(void) {
    Regex re;
    regex_init(&re, "(.*?)hi", NULL);
    RegexCompileResult comp_res = regex_compile(&re);
    T_ASSERT_EQ(REGEX_COMPILE_SUCCESS, comp_res);
    /*regex_output_ast(&re);*/
    char *str = "wowhihihi";
    MatchData mdata = regex_match(&re, str);
    T_ASSERT_EQ(true, mdata.matched);
    T_ASSERT_EQ(0, mdata.match_start);
    T_ASSERT_EQ(5, mdata.match_len);
    T_ASSERT_EQ(NODE_GROUP, re.groups->group->type);
    T_ASSERT_EQ(str+0, re.groups->group->capture_beg);
    T_ASSERT_EQ(str+3, re.groups->group->capture_end);
cleanup:
    regex_free(&re);
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    initCoreSighandlers();

    INIT_TESTS();
    RUN_TEST(test_compile_empty);
    RUN_TEST(test_match_empty);
    RUN_TEST(test_compile_only_atoms_success);
    RUN_TEST(test_match_only_atoms_success);
    RUN_TEST(test_match_only_atoms_nomatch);
    RUN_TEST(test_match_only_atoms_with_alts_success);
    RUN_TEST(test_match_only_atoms_with_alts_no_parens_success);
    RUN_TEST(test_match_only_atoms_with_alts_no_parens_repeat_success);
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
    RUN_TEST(test_compile_line_anchors);
    RUN_TEST(test_compile_string_anchors);
    RUN_TEST(test_match_bol_anchor);
    RUN_TEST(test_match_bol_anchor_at_line);
    RUN_TEST(test_nomatch_bol_anchor);
    RUN_TEST(test_match_bos_anchor);
    RUN_TEST(test_nomatch_bos_anchor);
    RUN_TEST(test_match_eol_anchor);
    RUN_TEST(test_match_eol_anchor_at_line);
    RUN_TEST(test_nomatch_eol_anchor);
    RUN_TEST(test_match_eos_anchor);
    RUN_TEST(test_nomatch_eos_anchor);
    RUN_TEST(test_capture_groups_nodes);
    RUN_TEST(test_capture_groups_nodes_with_nonatom);
    RUN_TEST(test_capture_groups_nodes_with_nonatom2);
    RUN_TEST(test_capture_groups_nodes_with_nonatom3);
    RUN_TEST(test_repeat_maximal_munch);
    RUN_TEST(test_repeat_nomatch_if_next_not_matched);
    RUN_TEST(test_repeat_nongreedy_minimal_munch);
    RUN_TEST(test_repeatz_maximal_munch);
    RUN_TEST(test_repeatz_nomatch_if_next_not_matched);
    RUN_TEST(test_repeatz_nongreedy_minimal_munch);
    END_TESTS();
}
