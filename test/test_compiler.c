#include "test.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"
#include "memory.h"

// no optimizations
static int compNoOpt(char *src, Chunk *chunk, CompileErr *err) {
    bool oldNoOpt = compilerOpts.noOptimize;
    compilerOpts.noOptimize = true;
    int ret = compile_src(src, chunk, err);
    compilerOpts.noOptimize = oldNoOpt;
    return ret;
}

// optimizations applied
static int compWithOpt(char *src, Chunk *chunk, CompileErr *err) {
    bool oldNoOpt = compilerOpts.noOptimize;
    compilerOpts.noOptimize = false;
    int ret = compile_src(src, chunk, err);
    compilerOpts.noOptimize = oldNoOpt;
    return ret;
}

static int test_compile_addition(void) {
    char *src = "1+1;";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compNoOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_CONSTANT\t" "0000\t" "'1.00'\n"
                     "0002\t" "OP_CONSTANT\t" "0001\t" "'1.00'\n"
                     "0004\t" "OP_ADD\n"
                     "0005\t" "OP_POP\n"
                     "0006\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    freeChunk(&chunk);
    return 0;
}

static int test_compile_global_variable(void) {
    char *src = "var a; a = 1;";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compNoOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_NIL\n"
                     "0001\t" "OP_DEFINE_GLOBAL\t" "0000\t" "'a'\n"
                     "0003\t" "OP_CONSTANT\t"      "0001\t" "'1.00'\n"
                     "0005\t" "OP_SET_GLOBAL\t"    "0000\t" "'a'\n"
                     "0007\t" "OP_POP\n"
                     "0008\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    freeChunk(&chunk);
    return 0;
}

static int test_compile_local_variable(void) {
    char *src = "{ var a = 1; a; }";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compNoOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_CONSTANT\t"  "0000\t"      "'1.00'\n"
                     "0002\t" "OP_SET_LOCAL\t" "[slot 001]\n"
                     "0004\t" "OP_GET_LOCAL\t" "[slot 001]\n"
                     "0006\t" "OP_POP\n"
                     "0007\t" "OP_POP\n"
                     "0008\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    freeChunk(&chunk);
    return 0;
}

static int test_compile_classdecl(void) {
    char *src = "class Train { choo() { return 1; } }";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compNoOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_CLASS\t"          "0000\t"    "'Train'\n"
                     "0002\t" "OP_CLOSURE\t"        "0001\t"    "'<fun Train#choo>'\t" "(upvals: 000)\n"
                     "0004\t" "OP_METHOD\t"         "0002\t"    "'choo'\n"
                     "0006\t" "OP_DEFINE_GLOBAL\t"  "0000\t"    "'Train'\n"
                     "0008\t" "OP_LEAVE\n"
                     "-- Function Train#choo --\n"
                     "0000\t" "OP_CONSTANT\t"       "0000\t"    "'1.00'\n"
                     "0002\t" "OP_RETURN\n"
                     "----\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    freeChunk(&chunk);
    return 0;
}

static int test_compile_try_stmt_with_catch1(void) {
    char *src = "class MyError { }\n"
                "try {\n"
                "print \"throwing\";\n"
                "throw MyError();\n"
                "print \"shouldn't get here!!\";\n"
                "} catch (MyError e) {\n"
                "  print e;\n"
                "}";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compNoOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "-- catch table --\n"
                     "0000) from: 0004, to: 0017, target: 0017, value: MyError\n"
                     "-- /catch table --\n"
                     "0000\t" "OP_CLASS\t"          "0000\t"    "'MyError'\n"
                     "0002\t" "OP_DEFINE_GLOBAL\t"  "0000\t"    "'MyError'\n"
                     "0004\t" "OP_CONSTANT\t"       "0001\t"    "'throwing'\n"
                     "0006\t" "OP_PRINT\n"
                     "0007\t" "OP_GET_GLOBAL\t"     "0000\t"    "'MyError'\n"
                     "0009\t" "OP_CALL\t"           "(argc=0000)\n"
                     "0011\t" "OP_THROW\n"
                     "0012\t" "OP_CONSTANT\t"       "0002\t"    "'shouldn't get here!!'\n"
                     "0014\t" "OP_PRINT\n"
                     "0015\t" "OP_JUMP\t"           "0011\t"    "(addr=0027)\n"
                     "0017\t" "OP_GET_THROWN\t"     "0003\t"    "'0.00'\n"
                     "0019\t" "OP_SET_LOCAL\t"      "[slot 001]\n"
                     "0021\t" "OP_GET_LOCAL\t"      "[slot 001]\n"
                     "0023\t" "OP_PRINT\n"
                     "0024\t" "OP_JUMP\t"           "0002\t"       "(addr=0027)\n"
                     "0026\t" "OP_POP\n"
                     "0027\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    freeChunk(&chunk);
    return 0;
}

static int test_compile_try_stmt_with_catch2(void) {
    char *src = "class MyError { }\n"
                "class MyError2 { }\n"
                "try {\n"
                "  print \"throwing\";\n"
                "  throw MyError();\n"
                "  print \"shouldn't get here!!\";\n"
                "} catch (MyError2 e) {\n"
                "  print e;\n"
                "} catch (MyError e) {\n"
                "  print e;\n"
                "}\n";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compNoOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "-- catch table --\n"
                    "0000) from: 0008, to: 0021, target: 0021, value: MyError2\n"
                    "0001) from: 0008, to: 0021, target: 0031, value: MyError\n"
                    "-- /catch table --\n"
                    "0000\t" "OP_CLASS\t"	          "0000\t"	"'MyError'\n"
                    "0002\t" "OP_DEFINE_GLOBAL\t"	  "0000\t"	"'MyError'\n"
                    "0004\t" "OP_CLASS\t"	          "0001\t"	"'MyError2'\n"
                    "0006\t" "OP_DEFINE_GLOBAL\t"	  "0001\t"	"'MyError2'\n"
                    "0008\t" "OP_CONSTANT\t"	      "0002\t"	"'throwing'\n"
                    "0010\t" "OP_PRINT\n"
                    "0011\t" "OP_GET_GLOBAL\t"	    "0000\t"	"'MyError'\n"
                    "0013\t" "OP_CALL\t"	          "(argc=0000)\n"
                    "0015\t" "OP_THROW\n"
                    "0016\t" "OP_CONSTANT\t"	      "0003\t"	"'shouldn't get here!!'\n"
                    "0018\t" "OP_PRINT\n"
                    "0019\t" "OP_JUMP\t"            "0021\t"  "(addr=0041)\n"
                    "0021\t" "OP_GET_THROWN\t"	    "0004\t"	"'0.00'\n"
                    "0023\t" "OP_SET_LOCAL\t"	      "[slot 001]\n"
                    "0025\t" "OP_GET_LOCAL\t"	      "[slot 001]\n"
                    "0027\t" "OP_PRINT\n"
                    "0028\t" "OP_JUMP\t"	          "0012\t"	  "(addr=0041)\n"
                    "0030\t" "OP_POP\n"
                    "0031\t" "OP_GET_THROWN\t"	    "0005\t"	"'1.00'\n"
                    "0033\t" "OP_SET_LOCAL\t"	      "[slot 001]\n"
                    "0035\t" "OP_GET_LOCAL\t"	      "[slot 001]\n"
                    "0037\t" "OP_PRINT\n"
                    "0038\t" "OP_JUMP\t"             "0002\t"	  "(addr=0041)\n"
                    "0040\t" "OP_POP\n"
                    "0041\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    freeChunk(&chunk);
    return 0;
}

int test_pop_assign_if_parent_stmt(void) {
    char *src = "var i = 0;\n"
                "while (i < 300) {\n"
                "  print i;\n"
                "  i = i+1;\n"
                "}";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compNoOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_CONSTANT\t"	"0000\t"	"'0.00'\n"
                     "0002\t"	"OP_DEFINE_GLOBAL\t"	"0001\t"	"'i'\n"
                     "0004\t"	"OP_GET_GLOBAL\t"	"0001\t"	"'i'\n"
                     "0006\t"	"OP_CONSTANT\t"	"0002\t"	"'300.00'\n"
                     "0008\t"	"OP_LESS\n"
                     "0009\t"	"OP_JUMP_IF_FALSE\t"	"0014\t"	"(addr=0024)\n"
                     "0011\t"	"OP_GET_GLOBAL\t"	"0001\t"	"'i'\n"
                     "0013\t"	"OP_PRINT\n"
                     "0014\t"	"OP_GET_GLOBAL\t"	"0001\t"	"'i'\n"
                     "0016\t"	"OP_CONSTANT\t"	"0003\t"	"'1.00'\n"
                     "0018\t"	"OP_ADD\n"
                     "0019\t"	"OP_SET_GLOBAL\t"	"0001\t"	"'i'\n"
                     "0021\t"	"OP_POP\n"
                     "0022\t"	"OP_LOOP\t"	  "  18\t"	"(addr=0004)\n"
                     "0024\t"	"OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    freeChunk(&chunk);
    return 0;
}

// only 1 return emitted per scope level
int test_spam_return(void) {
    char *src = "fun ret() { return \"HI\"; return \"AGAIN\"; }";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compNoOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t"	"OP_CLOSURE\t"	  "0000\t"	"'<fun ret>'\t" "(upvals: 000)\n"
                     "0002\t"	"OP_SET_GLOBAL\t"	"0001\t"	"'ret'\n"
                     "0004\t"	"OP_LEAVE\n"
                     "-- Function ret --\n"
                     "0000\t"	"OP_CONSTANT\t"	"0000\t"	"'HI'\n"
                     "0002\t"	"OP_RETURN\n"
                     "----\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

int test_upvalues_in_functions(void) {
    char *src = "var a = 1; fun add(b) { return fun(c) {  return a + b + c; }; }";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compNoOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t"	"OP_CONSTANT\t"       "0000\t"	"'1.00'\n"
                     "0002\t"	"OP_DEFINE_GLOBAL\t"	"0001\t"	"'a'\n"
                     "0004\t"	"OP_CLOSURE\t"	      "0002\t"	"'<fun add>'\t"	"(upvals: 000)\n"
                     "0006\t"	"OP_SET_GLOBAL\t"	    "0003\t"	"'add'\n"
                     "0008\t"	"OP_LEAVE\n"
                     "-- Function add --\n"
                     "0000\t"	"OP_CLOSURE\t"	      "0000\t"	  "'<fun (Anon)>'\t"	"(upvals: 001)\n"
                     "0004\t"	"OP_RETURN\n"
                     "-- Function (anon) --\n"
                     "0000\t"	"OP_GET_GLOBAL\t"	    "0000\t"	"'a'\n"
                     "0002\t"	"OP_GET_UPVALUE\t"	  "[slot 000]\n"
                     "0004\t"	"OP_ADD\n"
                     "0005\t"	"OP_GET_LOCAL\t"  	  "[slot 001]\n"
                     "0007\t"	"OP_ADD\n"
                     "0008\t"	"OP_RETURN\n"
                     "0009\t"	"OP_POP\n"
                     "----\n"
                     "----\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_compile_invoke(void) {
    char *src = "m.foo();";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compNoOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    char *expected = "0000\t"	"OP_GET_GLOBAL\t"	"0000\t"	"'m'\n"
                     "0002\t"	"OP_INVOKE\t"	"('foo', argc=0000)\n"
                     "0005\t"	"OP_POP\n"
                     "0006\t"	"OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_simple_constant_folding_opt(void) {
    char *src = "print 1+1;";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compWithOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    char *expected = "0000\t"	"OP_CONSTANT\t"	"0000\t"	"'2.00'\n"
                     "0002\t"	"OP_PRINT\n"
                     "0003\t"	"OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_complex_constant_folding_opt(void) {
    char *src = "print 1+2*8/4+1;";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compWithOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    char *expected = "0000\t"	"OP_CONSTANT\t"	"0000\t"	"'6.00'\n"
                     "0002\t"	"OP_PRINT\n"
                     "0003\t"	"OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_jump_consolidation_and_unused_expression_removal(void) {
    char *src = "if (true) { if (true) { } }";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compWithOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    char *expected = "0000\t"	  "OP_TRUE\n"
                      "0001\t"	"OP_POP\n"
                      "0002\t"	"OP_TRUE\n"
                      "0003\t"	"OP_JUMP_IF_FALSE\t"	"0001\t"	"(addr=0005)\n"
                      "0005\t"	"OP_LEAVE\n";

    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_while_true(void) {
    char *src = "while (true) { print 1; }";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compWithOpt(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    char *expected = "0000\t"	"OP_TRUE\n"
                     "0001\t"	"OP_JUMP_IF_FALSE\t"	"0006\t"	"(addr=0008)\n"
                     "0003\t"	"OP_CONSTANT\t"	"0000\t"	"'1.00'\n"
                     "0005\t"	"OP_PRINT\n"
                     "0006\t"	"OP_LOOP\t"  "   6\t"	"(addr=0000)\n"
                     "0008\t"	"OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    initVM();
    turnGCOff();
    INIT_TESTS();
    RUN_TEST(test_compile_addition);
    RUN_TEST(test_compile_global_variable);
    RUN_TEST(test_compile_local_variable);
    RUN_TEST(test_compile_classdecl);
    RUN_TEST(test_compile_try_stmt_with_catch1);
    RUN_TEST(test_compile_try_stmt_with_catch2);
    RUN_TEST(test_pop_assign_if_parent_stmt);
    RUN_TEST(test_spam_return);
    RUN_TEST(test_upvalues_in_functions);
    RUN_TEST(test_compile_invoke);

    // optimizations
    RUN_TEST(test_simple_constant_folding_opt);
    RUN_TEST(test_complex_constant_folding_opt);
    RUN_TEST(test_jump_consolidation_and_unused_expression_removal);
    RUN_TEST(test_while_true);

    END_TESTS();
}
