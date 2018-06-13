#include "test.h"
#include "nodes.h"
#include "parser.h"

// replace some blank characters with visible characters so it's easier
// to diff 2 strings. Replace spaces, tabs and newlines
static char *stringReplaceBlanks(char *str, char replacement) {
    int len = strlen(str);
    char *buf = calloc(len+1, 1);
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
    char *newbuf = calloc(newlen+1, 1);
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
        .type = TOKEN_STRING,
        .start = "\"testing\n\"",
        .length = strlen("\"testing\n\"")+1,
        .line = 1
    };
    n = createNode(nType, strTok, NULL);
    output = outputASTString(n, 0);
    T_ASSERT(strcmp(output, "\"testing\n\"") == 0);
cleanup:
    if (n != NULL) freeNode(n, true);
    if (output != NULL) free(output);
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
    T_ASSERT(strcmp(output, "1.0") == 0);
cleanup:
    if (n != NULL) freeNode(n, true);
    if (output != NULL) free(output);
    return 0;
}

static int test_output_nodes_from_parser_vardecl(void) {
    const char *src = "var a = 1;";
    initScanner(src);
    Node *program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT(strcmp("(varDecl a 1)\n", output) == 0);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_funcdecl(void) {
    const char *src = "fun f(a, b) {}";
    initScanner(src);
    Node *program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT(strcmp("(fnDecl f (a b)\n"
                    "  (block)\n"
                    ")\n", output) == 0);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_print(void) {
    const char *src = "print \"hi\";";
    initScanner(src);
    Node *program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    T_ASSERT(strcmp("(print \"hi\")\n", output) == 0);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_classdecl1(void) {
    const char *src = "class A {}";
    initScanner(src);
    Node *program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT(strcmp("(classDecl A\n"
                    "  (block)\n\n"
                    ")\n", output) == 0);

    src = "class A < B { }";
    initScanner(src);
    program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT(strcmp("(classDecl A B\n"
                    "  (block)\n\n"
                    ")\n", output) == 0);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_if1(void) {
    const char *src = "if (nil) {\n"
                      "  print \"got nil\";\n"
                      "} else { print \"not nil\"; }";
    initScanner(src);
    Node *program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT(strcmp("(if nil\n"
                    "(block\n"
                    "    (print \"got nil\")\n"
                    ")\n"
                    "(else\n"
                    "(block\n"
                    "    (print \"not nil\")\n"
                    ")\n)\n", output) == 0);

cleanup:
    return 0;
}

static int test_output_nodes_from_parser_while1(void) {
    const char *src = "while (true) {\n"
                      "  print \"again...\";\n"
                      "}";
    initScanner(src);
    Node *program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT(strcmp("(while true\n"
                    "(block\n"
                    "    (print \"again...\")\n"
                    ")\n)\n", output) == 0);

cleanup:
    return 0;
}

static int test_output_nodes_from_parser_for1(void) {
    const char *src = "for (;;) {\n"
                      "  print \"again...\";\n"
                      "}";
    initScanner(src);
    Node *program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT(strcmp("(for nil true nil\n"
                    "(block\n"
                    "    (print \"again...\")\n"
                    ")\n)\n", output) == 0);

cleanup:
    return 0;
}

static int test_output_nodes_from_parser_try1(void) {
    const char *src = "try {\n"
                      "  print \"again...\";\n"
                      "} catch (\"uh oh\") { }";
    initScanner(src);
    Node *program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT(strcmp("(try\n"
                    "(block\n"
                    "    (print \"again...\")\n"
                    ")\n"
                    "(catch \"uh oh\"\n"
                    "  (block)\n"
                    ")\n"
                    ")\n" , output) == 0);

cleanup:
    return 0;
}

static int test_output_nodes_from_parser_throw1(void) {
    const char *src = "throw \"UH OH\";";
    initScanner(src);
    Node *program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    T_ASSERT(strcmp("(throw \"UH OH\")\n", output) == 0);

cleanup:
    return 0;
}

static int test_output_nodes_from_parser_return1(void) {
    const char *src = "fun a() { return; }";
    initScanner(src);
    Node *program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(fnDecl a ()\n"
                     "(block\n"
                     "    (return)\n"
                     ")\n"
                     ")\n";
    T_ASSERT(strcmp(expected, output) == 0);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_assign1(void) {
    const char *src = "var a; a = 1;";
    initScanner(src);
    Node *program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(varDecl a)\n"
                     "(assign (var a) 1)\n";
    T_ASSERT(strcmp(expected, output) == 0);
cleanup:
    return 0;
}

static int test_output_nodes_from_parser_array1(void) {
    const char *src = "var a = [1,2,\"three\"];";
    initScanner(src);
    Node *program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    char *output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    char *expected = "(varDecl a (array 1 2 \"three\"))\n";
    T_ASSERT(strcmp(expected, output) == 0);

    src = "[1,2,3,4,[5],];";
    initScanner(src);
    program = parse();
    T_ASSERT(!parser.hadError);
    T_ASSERT(!parser.panicMode);
    output = outputASTString(program, 0);
    /*fprintf(stderr, "\n'%s'\n", output);*/
    expected = "(array 1 2 3 4 (array 5))\n";
    T_ASSERT(strcmp(expected, output) == 0);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    INIT_TESTS();
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
    END_TESTS();
}
