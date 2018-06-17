#include "test.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"

static InterpretResult interp(char *src, bool expectSuccess) {
    CompileErr cerr = COMPILE_ERR_NONE;
    InterpretResult ires = INTERPRET_OK;
    initVM();

    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &cerr);
    if (expectSuccess) {
        T_ASSERT_EQ(0, result);
        T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    }
    ires = interpret(&chunk);
    if (expectSuccess) {
        T_ASSERT_EQ(INTERPRET_OK, ires);
    }
cleanup:
    return ires;
}

static int test_addition(void) {
    char *src = "1+1;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_NUMBER(*val));
    T_ASSERT_EQ(2.0, AS_NUMBER(*val));
cleanup:
    return 0;
}

static int test_subtraction(void) {
    char *src = "1-3;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_NUMBER(*val));
    T_ASSERT_EQ(-2.0, AS_NUMBER(*val));
cleanup:
    return 0;
}

static int test_negation(void) {
    char *src = "---2.0;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_NUMBER(*val));
    T_ASSERT_EQ(-2.0, AS_NUMBER(*val));
cleanup:
    return 0;
}

static int test_print_number(void) {
    char *src = "print 2.0;";
    interp(src, true);
cleanup:
    return 0;
}

static int test_print_string(void) {
    char *src = "print \"howdy\";";
    interp(src, true);
cleanup:
    return 0;
}

static int test_global_vars1(void) {
    char *src = "var greet = \"howdy\";"
                "greet;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_STRING(*val));
    T_ASSERT_STREQ("howdy", AS_CSTRING(*val));
cleanup:
    return 0;
}

static int test_simple_and(void) {
    char *src = "true and false;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_BOOL(*val));
    T_ASSERT_EQ(false, AS_BOOL(*val));
cleanup:
    return 0;
}

static int test_simple_or(void) {
    char *src = "false or true;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_BOOL(*val));
    T_ASSERT_EQ(true, AS_BOOL(*val));
cleanup:
    return 0;
}

static int test_simple_if(void) {
    char *src = "if (false) { print(\"woops\"); \"woops\"; } else { print \"jumped\"; \"jumped\"; }";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_STRING(*val));
    T_ASSERT_STREQ("jumped", AS_CSTRING(*val));
cleanup:
    return 0;
}

static int test_vardecls_in_block_not_global(void) {
    char *src = "var a = \"outer\"; if (true) { var a = \"in block\"; a; }";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_STRING(*val));
    T_ASSERT_STREQ("in block", AS_CSTRING(*val));
cleanup:
    return 0;
}

static int test_simple_while_loop(void) {
    char *src = "var i = 0; while (i < 10) { print i; i = i + 1; } i;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_NUMBER(*val));
    T_ASSERT_EQ(10.0, AS_NUMBER(*val));
cleanup:
    return 0;
}

static int test_simple_function(void) {
    char *src = "fun f() { return \"FUN\"; } var ret = f(); ret;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_STRING(*val));
    T_ASSERT_STREQ("FUN", AS_CSTRING(*val));
cleanup:
    return 0;
}

static int test_simple_class(void) {
    char *src = "class Train {} var t = Train(); print t; t;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_INSTANCE(*val));
    T_ASSERT_VALPRINTEQ("<instance Train>", *val);
cleanup:
    return 0;
}

static int test_simple_class_initializer(void) {
    char *src = "class Train {\n"
                "  fun init(color) {\n"
                "    this.color = color;\n"
                "  }\n"
                "}\n"
                "var t = Train(\"Red\");\n"
                "print t.color;\n"
                "t.color;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_STRING(*val));
    T_ASSERT_VALPRINTEQ("Red", *val);
cleanup:
    return 0;
}

static int test_simple_class_initializer2(void) {
    char *src = "class Train {\n"
                "  fun init(color) {\n"
                "    return \"non-instance!\";\n"
                "  }\n"
                "}\n"
                "var t = Train(\"Red\");\n"
                "t;\n";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_INSTANCE(*val));
cleanup:
    return 0;
}

static int test_simple_method1(void) {
    char *src = "class Train {\n"
                "  fun choo() { print \"choo\"; return this; }\n"
                "}\n"
                "var t = Train();\n"
                "t.choo().choo();\n";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_INSTANCE(*val));
cleanup:
    return 0;
}

static int test_native_clock(void) {
    char *src = "print clock(); clock();";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_NUMBER(*val));
cleanup:
    return 0;
}

static int test_native_clock_bad_args(void) {
    char *src = "print clock(\"uh oh\");";
    InterpretResult ires = interp(src, false);
    T_ASSERT_EQ(INTERPRET_RUNTIME_ERROR, ires);
cleanup:
    return 0;
}

static int test_throw_catch(void) {
    char *src = "class MyError { }\n"
                "try {\n"
                "print \"throwing\";\n"
                "throw MyError();\n"
                "print \"shouldn't get here!!\";\n"
                "} catch (MyError e) {\n"
                "  print e;\n"
                "}";
    InterpretResult ires = interp(src, true);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    INIT_TESTS();
    RUN_TEST(test_addition);
    RUN_TEST(test_subtraction);
    RUN_TEST(test_negation);
    RUN_TEST(test_print_number);
    RUN_TEST(test_print_string);
    RUN_TEST(test_global_vars1);
    RUN_TEST(test_simple_and);
    RUN_TEST(test_simple_or);
    RUN_TEST(test_simple_if);
    RUN_TEST(test_vardecls_in_block_not_global);
    RUN_TEST(test_simple_while_loop);
    RUN_TEST(test_simple_function);
    RUN_TEST(test_simple_class);
    RUN_TEST(test_simple_class_initializer);
    RUN_TEST(test_simple_class_initializer2);
    RUN_TEST(test_simple_method1);
    RUN_TEST(test_native_clock);
    RUN_TEST(test_native_clock_bad_args);
    RUN_TEST(test_throw_catch);
    END_TESTS();
}
