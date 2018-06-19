#include "test.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"
#include "memory.h"

static int test_compile_addition(void) {
    char *src = "1+1;";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_CONSTANT\t" "0000\t" "'1.00'\n"
                     "0002\t" "OP_CONSTANT\t" "0001\t" "'1.00'\n"
                     "0004\t" "OP_ADD\n"
                     "0005\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_compile_global_variable(void) {
    char *src = "var a; a = 1;";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_NIL\n"
                     "0001\t" "OP_DEFINE_GLOBAL\t" "0000\t" "'a'\n"
                     "0003\t" "OP_CONSTANT\t"      "0002\t" "'1.00'\n"
                     "0005\t" "OP_SET_GLOBAL\t"    "0001\t" "'a'\n"
                     "0007\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_compile_local_variable(void) {
    char *src = "{ var a = 1; a; }";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_CONSTANT\t"  "0000\t"      "'1.00'\n"
                     "0002\t" "OP_SET_LOCAL\t" "[slot 001]\n"
                     "0004\t" "OP_GET_LOCAL\t" "[slot 001]\n"
                     "0006\t" "OP_POP\n"
                     "0007\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

static int test_compile_classdecl(void) {
    char *src = "class Train { fun choo() { return 1; } }";
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &err);
    T_ASSERT_EQ(0, result);
    ObjString *string = disassembleChunk(&chunk);
    char *cstring = string->chars;
    /*fprintf(stderr, "\n'%s'\n", cstring);*/
    char *expected = "0000\t" "OP_CLASS\t"          "0000\t"    "'Train'\n"
                     "0002\t" "OP_CONSTANT\t"       "0001\t"    "'<fun Train.choo>'\n"
                     "0004\t" "OP_METHOD\t"         "0002\t"    "'choo'\n"
                     "0006\t" "OP_DEFINE_GLOBAL\t"  "0000\t"    "'Train'\n"
                     "0008\t" "OP_LEAVE\n"
                     "-- Function Train.choo --\n"
                     "0000\t" "OP_CONSTANT\t"       "0000\t"    "'1.00'\n"
                     "0002\t" "OP_RETURN\n"
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
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &err);
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
                     "0007\t" "OP_GET_GLOBAL\t"     "0002\t"    "'MyError'\n"
                     "0009\t" "OP_CALL\t"           "(argc=0000)\n"
                     "0011\t" "OP_THROW\n"
                     "0012\t" "OP_CONSTANT\t"       "0003\t"    "'shouldn't get here!!'\n"
                     "0014\t" "OP_PRINT\n"
                     "0015\t" "OP_JUMP\t"           "0011\t"    "(addr=0027)\n"
                     "0017\t" "OP_GET_THROWN\t"     "0004\t"    "'0.00'\n"
                     "0019\t" "OP_SET_LOCAL\t"      "[slot 001]\n"
                     "0021\t" "OP_GET_LOCAL\t"      "[slot 001]\n"
                     "0023\t" "OP_PRINT\n"
                     "0024\t" "OP_JUMP\t"           "0002\t"       "(addr=0027)\n"
                     "0026\t" "OP_POP\n"
                     "0027\t" "OP_LEAVE\n";
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
    CompileErr err = COMPILE_ERR_NONE;
    Chunk chunk;
    initChunk(&chunk);
    int result = compile_src(src, &chunk, &err);
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
                    "0011\t" "OP_GET_GLOBAL\t"	    "0003\t"	"'MyError'\n"
                    "0013\t" "OP_CALL\t"	          "(argc=0000)\n"
                    "0015\t" "OP_THROW\n"
                    "0016\t" "OP_CONSTANT\t"	      "0004\t"	"'shouldn't get here!!'\n"
                    "0018\t" "OP_PRINT\n"
                    "0019\t" "OP_JUMP\t"            "0021\t"  "(addr=0041)\n"
                    "0021\t" "OP_GET_THROWN\t"	    "0005\t"	"'0.00'\n"
                    "0023\t" "OP_SET_LOCAL\t"	      "[slot 001]\n"
                    "0025\t" "OP_GET_LOCAL\t"	      "[slot 001]\n"
                    "0027\t" "OP_PRINT\n"
                    "0028\t" "OP_JUMP\t"	          "0012\t"	  "(addr=0041)\n"
                    "0030\t" "OP_POP\n"
                    "0031\t" "OP_GET_THROWN\t"	    "0007\t"	"'1.00'\n"
                    "0033\t" "OP_SET_LOCAL\t"	      "[slot 001]\n"
                    "0035\t" "OP_GET_LOCAL\t"	      "[slot 001]\n"
                    "0037\t" "OP_PRINT\n"
                    "0038\t" "OP_JUMP\t"             "0002\t"	  "(addr=0041)\n"
                    "0040\t" "OP_POP\n"
                    "0041\t" "OP_LEAVE\n";
    T_ASSERT_STREQ(expected, cstring);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    turnGCOff(); // For now, there's a bug in there somewhere
    parseTestOptions(argc, argv);
    initVM();
    INIT_TESTS();
    RUN_TEST(test_compile_addition);
    RUN_TEST(test_compile_global_variable);
    RUN_TEST(test_compile_local_variable);
    RUN_TEST(test_compile_classdecl);
    RUN_TEST(test_compile_try_stmt_with_catch1);
    RUN_TEST(test_compile_try_stmt_with_catch2);
    END_TESTS();
}
