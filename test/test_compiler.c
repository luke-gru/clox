#include "test.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"
#include "memory.h"

// no optimizations
static Chunk *compNoOpt(char *src, CompileErr *err) {
    bool oldNoOpt = compilerOpts.noOptimize;
    compilerOpts.noOptimize = true;
    Chunk *ret = compile_src(src, err);
    compilerOpts.noOptimize = oldNoOpt;
    return ret;
}

// optimizations applied
static Chunk *compWithOpt(char *src, CompileErr *err) {
    bool oldNoOpt = compilerOpts.noOptimize;
    compilerOpts.noOptimize = false;
    Chunk *ret = compile_src(src, err);
    compilerOpts.noOptimize = oldNoOpt;
    return ret;
}

static int test_compile_addition(void) {
    char *src = "1+1;";
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compNoOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_CONSTANT\t" "0000\t" "'1'\n"
                     "0002\t" "OP_CONSTANT\t" "0001\t" "'1'\n"
                     "0004\t" "OP_ADD\n"
                     "0005\t" "OP_POP\n"
                     "0006\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_compile_global_variable(void) {
    char *src = "var a; a = 1;";
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compNoOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_NIL\n"
                     "0001\t" "OP_DEFINE_GLOBAL\t" "0000\t" "'a'\n"
                     "0003\t" "OP_CONSTANT\t"      "0001\t" "'1'\n"
                     "0005\t" "OP_SET_GLOBAL\t"    "0000\t" "'a'\n"
                     "0007\t" "OP_POP\n"
                     "0008\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_compile_local_variable(void) {
    char *src = "{ var a = 1; a; }";
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compNoOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_CONSTANT\t"  "0000\t"      "'1'\n"
                     "0002\t" "OP_SET_LOCAL\t" "'a' [slot 000]\n"
                     "0005\t" "OP_GET_LOCAL\t" "'a' [slot 000]\n"
                     "0008\t" "OP_POP\n"
                     "0009\t" "OP_POP\n"
                     "0010\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_compile_classdecl(void) {
    char *src = "class Train { choo() { return 1; } }";
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compNoOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_CLASS\t"          "0000\t"    "'Train'\n"
                     "0002\t" "OP_CLOSURE\t"        "0001\t"    "'<fun Train#choo>'\t" "(upvals: 000)\n"
                     "0004\t" "OP_METHOD\t"         "0002\t"    "'choo'\n"
                     "0006\t" "OP_DEFINE_GLOBAL\t"  "0000\t"    "'Train'\n"
                     "0008\t" "OP_LEAVE\n"
                     "-- Function Train#choo --\n"
                     "0000\t" "OP_CONSTANT\t"       "0000\t"    "'1'\n"
                     "0002\t" "OP_RETURN\n"
                     // TODO: remove these instructions
                     "0003\t" "OP_NIL\n"
                     "0004\t" "OP_RETURN\n"
                     "----\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
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
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compNoOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "-- catch table --\n"
                     "0000) from: 0004, to: 0020, target: 0020, value: MyError\n"
                     "-- /catch table --\n"
                     "0000\t" "OP_CLASS\t"          "0000\t"    "'MyError'\n"
                     "0002\t" "OP_DEFINE_GLOBAL\t"  "0000\t"    "'MyError'\n"
                     "0004\t" "OP_STRING\t"         "0001\t"    "'throwing' (static=0)\n"
                     "0007\t" "OP_PRINT\n"
                     "0008\t" "OP_GET_GLOBAL\t"     "0000\t"    "'MyError'\n"
                     "0010\t" "OP_CALL\t"           "(argc=00)\n"
                     "0013\t" "OP_THROW\n"
                     "0014\t" "OP_STRING\t"         "0003\t"    "'shouldn't get here!!' (static=0)\n"
                     "0017\t" "OP_PRINT\n"
                     "0018\t" "OP_JUMP\t"           "0011\t"    "(addr=0030)\n"
                     "0020\t" "OP_GET_THROWN\t"     "0004\t"    "'0'\n"
                     "0022\t" "OP_SET_LOCAL\t"      "'e' [slot 000]\n"
                     "0025\t" "OP_GET_LOCAL\t"      "'e' [slot 000]\n"
                     "0028\t" "OP_PRINT\n"
                     "0029\t" "OP_POP\n"
                     "0030\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
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
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compNoOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "-- catch table --\n"
                    "0000) from: 0008, to: 0024, target: 0024, value: MyError2\n"
                    "0001) from: 0008, to: 0024, target: 0036, value: MyError\n"
                    "-- /catch table --\n"
                    "0000\t" "OP_CLASS\t"	          "0000\t"	"'MyError'\n"
                    "0002\t" "OP_DEFINE_GLOBAL\t"	  "0000\t"	"'MyError'\n"
                    "0004\t" "OP_CLASS\t"	          "0001\t"	"'MyError2'\n"
                    "0006\t" "OP_DEFINE_GLOBAL\t"	  "0001\t"	"'MyError2'\n"
                    "0008\t" "OP_STRING\t"	        "0002\t"	"'throwing' (static=0)\n"
                    "0011\t" "OP_PRINT\n"
                    "0012\t" "OP_GET_GLOBAL\t"	    "0000\t"	"'MyError'\n"
                    "0014\t" "OP_CALL\t"            "(argc=00)\n"
                    "0017\t" "OP_THROW\n"
                    "0018\t" "OP_STRING\t"	        "0004\t"	"'shouldn't get here!!' (static=0)\n"
                    "0021\t" "OP_PRINT\n"
                    "0022\t" "OP_JUMP\t"            "0023\t"  "(addr=0046)\n"
                    "0024\t" "OP_GET_THROWN\t"	    "0005\t"	"'0'\n"
                    "0026\t" "OP_SET_LOCAL\t"	      "'e' [slot 001]\n"
                    "0029\t" "OP_GET_LOCAL\t"	      "'e' [slot 001]\n"
                    "0032\t" "OP_PRINT\n"
                    "0033\t" "OP_JUMP\t"	          "0012\t"	  "(addr=0046)\n"
                    "0035\t" "OP_POP\n"
                    "0036\t" "OP_GET_THROWN\t"	    "0007\t"	"'1'\n"
                    "0038\t" "OP_SET_LOCAL\t"	      "'e' [slot 000]\n"
                    "0041\t" "OP_GET_LOCAL\t"	      "'e' [slot 000]\n"
                    "0044\t" "OP_PRINT\n"
                    "0045\t" "OP_POP\n"
                    "0046\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

int test_pop_assign_if_parent_stmt(void) {
    char *src = "var i = 0;\n"
                "while (i < 300) {\n"
                "  print i;\n"
                "  i = i+1;\n"
                "}";
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compNoOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_CONSTANT\t"	"0000\t"	"'0'\n"
                     "0002\t"	"OP_DEFINE_GLOBAL\t"	"0001\t"	"'i'\n"
                     "0004\t"	"OP_GET_GLOBAL\t"	"0001\t"	"'i'\n"
                     "0006\t"	"OP_CONSTANT\t"	"0002\t"	"'300'\n"
                     "0008\t"	"OP_LESS\n"
                     "0009\t"	"OP_JUMP_IF_FALSE\t"	"0014\t"	"(addr=0024)\n"
                     "0011\t"	"OP_GET_GLOBAL\t"	"0001\t"	"'i'\n"
                     "0013\t"	"OP_PRINT\n"
                     "0014\t"	"OP_GET_GLOBAL\t"	"0001\t"	"'i'\n"
                     "0016\t"	"OP_CONSTANT\t"	"0003\t"	"'1'\n"
                     "0018\t"	"OP_ADD\n"
                     "0019\t"	"OP_SET_GLOBAL\t"	"0001\t"	"'i'\n"
                     "0021\t"	"OP_POP\n"
                     "0022\t"	"OP_LOOP\t"	  "  18\t"	"(addr=0004)\n"
                     "0024\t"	"OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

// only 1 return emitted per scope level
int test_spam_return(void) {
    char *src = "fun ret() { return \"HI\"; return \"AGAIN\"; }";
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compNoOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t"	"OP_CLOSURE\t"	  "0000\t"	"'<fun ret>'\t" "(upvals: 000)\n"
                     "0002\t"	"OP_SET_GLOBAL\t"	"0001\t"	"'ret'\n"
                     "0004\t"	"OP_LEAVE\n"
                     "-- Function ret --\n"
                     "0000\t"	"OP_STRING\t"	    "0000\t"	"'HI' (static=0)\n"
                     "0003\t"	"OP_RETURN\n"
                     // TODO: remove these
                     "0004\t"	"OP_STRING\t"     "0001\t"  "'AGAIN' (static=0)\n"
                     "0007\t"	"OP_RETURN\n"
                     "0008\t"	"OP_NIL\n"
                     "0009\t"	"OP_RETURN\n"
                     "----\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

int test_upvalues_in_functions(void) {
    char *src = "var a = 1; fun add(b) { return fun(c) {  return a + b + c; }; }";
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compNoOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t"	"OP_CONSTANT\t"       "0000\t"	"'1'\n"
                     "0002\t"	"OP_DEFINE_GLOBAL\t"	"0001\t"	"'a'\n"
                     "0004\t"	"OP_CLOSURE\t"	      "0002\t"	"'<fun add>'\t"	"(upvals: 000)\n"
                     "0006\t"	"OP_SET_GLOBAL\t"	    "0003\t"	"'add'\n"
                     "0008\t"	"OP_LEAVE\n"
                     "-- Function add --\n"
                     "0000\t"	"OP_CLOSURE\t"	      "0000\t"	  "'<fun (Anon)>'\t"	"(upvals: 001)\n"
                     "0004\t"	"OP_RETURN\n"
                     // TODO: remove these next instructions
                     "0005\t"	"OP_CLOSE_UPVALUE\n"
                     "0006\t"	"OP_NIL\n"
                     "0007\t"	"OP_RETURN\n"
                     "-- Function (anon) --\n"
                     "0000\t"	"OP_GET_GLOBAL\t"	    "0000\t"	"'a'\n"
                     "0002\t"	"OP_GET_UPVALUE\t"	  "'b' [slot 000]\n"
                     "0005\t"	"OP_ADD\n"
                     "0006\t"	"OP_GET_LOCAL\t"  	  "'c' [slot 001]\n"
                     "0009\t"	"OP_ADD\n"
                     "0010\t"	"OP_RETURN\n"
                     // TODO: remove these next instructions
                     "0011\t"	"OP_POP\n"
                     "0012\t"	"OP_NIL\n"
                     "0013\t"	"OP_RETURN\n"
                     "----\n"
                     "----\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_compile_invoke(void) {
    char *src = "m.foo();";
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compNoOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    char *expected = "0000\t"	"OP_GET_GLOBAL\t"	"0000\t"	"'m'\n"
                     "0002\t"	"OP_INVOKE\t"	"('foo', argc=0000)\n"
                     "0006\t"	"OP_POP\n"
                     "0007\t"	"OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_simple_constant_folding_opt(void) {
    char *src = "print 1+1;";
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compWithOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    char *expected = "0000\t"	"OP_CONSTANT\t"	"0000\t"	"'2'\n"
                     "0002\t"	"OP_PRINT\n"
                     "0003\t"	"OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_complex_constant_folding_opt(void) {
    char *src = "print 1+2*8/4+1;";
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compWithOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    char *expected = "0000\t"	"OP_CONSTANT\t"	"0000\t"	"'6'\n"
                     "0002\t"	"OP_PRINT\n"
                     "0003\t"	"OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_jump_consolidation_and_unused_expression_removal(void) {
    char *src = "if (true) { if (true) { } }";
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compWithOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
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
    CompileErr cerr = COMPILE_ERR_NONE;
    Chunk *chunk = compWithOpt(src, &cerr);
    T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    ObjString *string = disassembleChunk(chunk);
    char *cstring = string->chars;
    char *expected = "0000\t"	"OP_TRUE\n"
                     "0001\t"	"OP_JUMP_IF_FALSE\t"	"0006\t"	"(addr=0008)\n"
                     "0003\t"	"OP_CONSTANT\t"	"0000\t"	"'1'\n"
                     "0005\t"	"OP_PRINT\n"
                     "0006\t"	"OP_LOOP\t"  "   6\t"	"(addr=0000)\n"
                     "0008\t"	"OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    initSighandlers();

    initVM();
    turnGCOff(); // FIXME: why is this here?
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

    freeVM();
    END_TESTS();
}
