#include "test.h"
#include "nodes.h"
#include "parser.h"
#include "vm.h"
#include "memory.h"

static Parser parser;

// replace some blank characters with visible characters so it's easier
// to diff 2 strings. Replace spaces, tabs and newlines
static char *stringReplaceBlanks(char *str, char replacement) {
    int len = strlen(str);
    char *buf = calloc(1, len+1);
    strcpy(buf, str);
    str = buf; // allow `str` to be a static string, so we copy it in this case
    int numNewlines = 0;
    int numTabs = 0;
    for (int i = 0; i < len; i++) {
        if (str[i] == ' ') {
            str[i] = replacement;
        } else if (str[i] == '\n') {
            numNewlines++;
        } else if (str[i] == '\t') {
            numTabs++;
        }
    }
    int newlen = len+(numNewlines*2)+(numTabs*2);
    char *newbuf = calloc(1, newlen+1);
    memcpy(newbuf, str, len+1);
    for (int i = 0; i < newlen; i++) {
        if (newbuf[i] == '\n') {
            memmove(newbuf+i+1, newbuf+i, newlen-i);
            newbuf[i] = '>'; // '>' before newlines
            i++;
        } else if (newbuf[i] == '\t') {
            memmove(newbuf+i+1, newbuf+i, newlen-i);
            newbuf[i] = 'T'; // 'T' before tabs
            i++;
        }
    }
    return newbuf;
}

static int test_output_node_literal_string(void) {
    Node *n = NULL;
    char *output = NULL;
    node_type_t nType = {
        .type = NODE_EXPR,
        .kind = LITERAL_EXPR,
        .litKind = STRING_TYPE,
    };
    Token strTok = {
        .type = TOKEN_STRING_DQUOTE,
        .start = "testing\n",
        .length = strlen("testing\n")+1,
        .line = 1
    };
    n = createNode(nType, strTok, NULL);
    output = outputASTString(n, 0);
    T_ASSERT_STREQ("\"testing\n\"", output);
cleanup:
    if (n != NULL) freeNode(n, true);
    if (output != NULL) xfree(output);
    return 0;
}

static int test_output_node_literal_number(void) {
    Node *n = NULL;
    char *output = NULL;
    node_type_t nType = {
        .type = NODE_EXPR,
        .kind = LITERAL_EXPR,
        .litKind = NUMBER_TYPE,
    };
    Token numTok = {
        .type = TOKEN_NUMBER,
        .start = "1.0",
        .length = strlen("1.0")+1,
        .line = 1
    };
    n = createNode(nType, numTok, NULL);
    output = outputASTString(n, 0);
    T_ASSERT_STREQ("1.0", output);
cleanup:
    if (n != NULL) freeNode(n, true);
    if (output != NULL) xfree(output);
    return 0;
}

static int test_output_nodes_from_parser_vardecl(void) {
    const char *src = "var a = 1;";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT_STREQ("(varDecl a 1)\n", output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_funcdecl(void) {
    const char *src = "fun f(a, b) {}";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT_STREQ("(fnDecl f (a b)\n"
                    "  (block)\n"
                    ")\n", output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_print(void) {
    const char *src = "print \"hi\";";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    T_ASSERT_STREQ("(print \"hi\")\n", output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_classdecl1(void) {
    const char *src = "class A {}";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT_STREQ("(classDecl A\n"
                    "  (block)\n\n"
                    ")\n", output);

    src = "class A < B { }";
    initScanner(&scanner, src);
    program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT_STREQ("(classDecl A B\n"
                    "  (block)\n\n"
                    ")\n", output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_if1(void) {
    const char *src = "if (nil) {\n"
                      "  print \"got nil\";\n"
                      "} else { print \"not nil\"; }";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT_STREQ("(if nil\n"
                    "  (block\n"
                    "    (print \"got nil\")\n"
                    "  )\n"
                    "(else\n"
                    "  (block\n"
                    "    (print \"not nil\")\n"
                    "  )\n"
                    ")\n", output);

cleanup:
    return 0;
}

static int test_output_nodes_from_parser_while1(void) {
    const char *src = "while (true) {\n"
                      "  print \"again...\";\n"
                      "}";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT_STREQ("(while true\n"
                    "  (block\n"
                    "    (print \"again...\")\n"
                    "  )\n"
                    ")\n", output);

cleanup:
    return 0;
}

static int test_output_nodes_from_parser_for1(void) {
    const char *src = "for (;;) {\n"
                      "  print \"again...\";\n"
                      "}";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT_STREQ("(for nil true nil\n"
                    "  (block\n"
                    "    (print \"again...\")\n"
                    "  )\n"
                    ")\n", output);

cleanup:
    return 0;
}

static int test_output_nodes_from_parser_try1(void) {
    const char *src = "try {\n"
                      "  print \"again...\";\n"
                      "} catch (\"uh oh\") { }";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT_STREQ("(try\n"
                    "  (block\n"
                    "    (print \"again...\")\n"
                    "  )\n"
                    "(catch \"uh oh\"\n"
                    "  (block)\n"
                    ")\n"
                    ")\n" , output);

cleanup:
    return 0;
}

static int test_output_nodes_from_parser_throw1(void) {
    const char *src = "throw \"UH OH\";";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT_STREQ("(throw \"UH OH\")\n", output);

cleanup:
    return 0;
}

static int test_output_nodes_from_parser_return1(void) {
    const char *src = "fun a() { return; }";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(fnDecl a ()\n"
                     "  (block\n"
                     "    (return)\n"
                     "  )\n"
                     ")\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_assign1(void) {
    const char *src = "var a; a = 1;";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(varDecl a)\n"
                     "(assign (var a) 1)\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_array1(void) {
    const char *src = "var a = [1,2,\"three\"];";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(varDecl a (array 1 2 \"three\"))\n";
    T_ASSERT_STREQ(expected, output);

    src = "[1,2,3,4,[5],];";
    initScanner(&scanner, src);
    program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    expected = "(array 1 2 3 4 (array 5))\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_binaryop1(void) {
    const char *src = "1+101;";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(+ 1 101)\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_logicalop1(void) {
    const char *src = "1 <= 101;";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(<= 1 101)\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_grouping1(void) {
    const char *src = "(\"in parens\");";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(group \"in parens\")\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_superexpr(void) {
    const char *src = "fun a(n) { return super.a(n); }";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(fnDecl a (n)\n"
                     "  (block\n"
                     "    (return (call (propGet super a) ((var n) ))\n"
                     "  )\n"
                     ")\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_thisexpr(void) {
    const char *src = "fun me() { return this; }";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(fnDecl me ()\n"
                     "  (block\n"
                     "    (return (var this))\n"
                     "  )\n"
                     ")\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_anonfn(void) {
    const char *src = "var f = fun() { return \"FUN\"; };";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(varDecl f (fnAnon ()\n"
                     "  (block\n"
                     "    (return \"FUN\")\n"
                     "  )\n"
                     ")\n"
                     ")\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_indexget(void) {
    const char *src = "var two = [1,2,3][1];";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(varDecl two (idxGet (array 1 2 3) 1))\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_indexset(void) {
    const char *src = "[1,2,3][1] = 1;";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(idxSet (array 1 2 3) 1 1)\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_propget(void) {
    const char *src = "expr.propname;";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(propGet (var expr) propname)\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_propset(void) {
    const char *src = "expr.propname = propval;";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(propSet (var expr) propname (var propval))\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_precedence1(void) {
    const char *src = "1+2*3*4;";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(+ 1 (* (* 2 3) 4))\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

static int test_parser_string_interpolation(void) {
    const char *src = "\"Hey ${name}, how's it going?\";\n";
    initScanner(&scanner, src);
    Node *program = parse(&parser);
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(+ \"Hey \" (+ (call (const String) ((var name) ) \", how's it going?\"))\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    initVM();
    INIT_TESTS("test_nodes");
    RUN_TEST(test_output_node_literal_string);
    RUN_TEST(test_output_node_literal_number);
    RUN_TEST(test_output_nodes_from_parser_vardecl);
    RUN_TEST(test_output_nodes_from_parser_funcdecl);
    RUN_TEST(test_output_nodes_from_parser_print);
    RUN_TEST(test_output_nodes_from_parser_classdecl1);
    RUN_TEST(test_output_nodes_from_parser_if1);
    RUN_TEST(test_output_nodes_from_parser_while1);
    RUN_TEST(test_output_nodes_from_parser_for1);
    RUN_TEST(test_output_nodes_from_parser_try1);
    RUN_TEST(test_output_nodes_from_parser_throw1);
    RUN_TEST(test_output_nodes_from_parser_return1);
    RUN_TEST(test_output_nodes_from_parser_assign1);
    RUN_TEST(test_output_nodes_from_parser_array1);
    RUN_TEST(test_output_nodes_from_parser_binaryop1);
    RUN_TEST(test_output_nodes_from_parser_logicalop1);
    RUN_TEST(test_output_nodes_from_parser_grouping1);
    RUN_TEST(test_output_nodes_from_parser_superexpr);
    RUN_TEST(test_output_nodes_from_parser_thisexpr);
    RUN_TEST(test_output_nodes_from_parser_anonfn);
    RUN_TEST(test_output_nodes_from_parser_indexget);
    RUN_TEST(test_output_nodes_from_parser_indexset);
    RUN_TEST(test_output_nodes_from_parser_propget);
    RUN_TEST(test_output_nodes_from_parser_propset);
    RUN_TEST(test_output_nodes_from_parser_precedence1);
    RUN_TEST(test_parser_string_interpolation);
    freeVM();
    END_TESTS();
}
