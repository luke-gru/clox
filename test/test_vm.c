#include "test.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"
#include "memory.h"
#include "object.h"

static InterpretResult interp(char *src, bool expectSuccess) {
    CompileErr cerr = COMPILE_ERR_NONE;
    InterpretResult ires = INTERPRET_OK;
    if (!vm.inited) initVM();

    Chunk *chunk = compile_src(src, &cerr);
    if (expectSuccess) {
        T_ASSERT(chunk != NULL);
        T_ASSERT_EQ(COMPILE_ERR_NONE, cerr);
    }
    ires = interpret(chunk, "test");
    resetStack();
    if (expectSuccess) {
        T_ASSERT_EQ(INTERPRET_OK, ires);
    }
cleanup:
    return ires;
}

static void raiseErr(int a) {
    throwErrorFmt(lxErrClass, "error %d", a);
    UNREACHABLE("bug");
}

static void* raiseErrProtect(void *arg) {
    raiseErr(*((int*)arg));
    UNREACHABLE("bug");
    return NULL;
}

static void* raiseNoErrProtect(void *arg) {
    return lxAryClass;
}

static int test_vm_protect1(void) {
    initVM();
    int arg = 3;
    ErrTag status = TAG_NONE;
    EC->frameCount = 0;
    CallFrame *frame = pushFrame();
    frame->start = 0;
    frame->ip = 0;
    frame->slots = EC->stack;
    frame->isCCall = false;
    frame->callLine = 1;
    frame->file = hiddenString("file", 4);
    // catch all errors of instance lxErrClass
    void *res = vm_protect(raiseErrProtect, &arg, lxErrClass, &status);
    T_ASSERT_EQ(TAG_RAISE, status);
    T_ASSERT_EQ(NULL, res);
    T_ASSERT_EQ(false, THREAD()->hadError);
    T_ASSERT(THREAD()->errInfo != NULL);
    popFrame();
    T_ASSERT_EQ(NULL, THREAD()->errInfo); // frame popped, errInfo for frame should be gone
cleanup:
    freeVM();
    return 0;
}

static int test_vm_protect2(void) {
    initVM();
    int arg = 4;
    ErrTag status = TAG_NONE;
    EC->frameCount = 0;
    CallFrame *frame = pushFrame();
    frame->start = 0;
    frame->ip = 0;
    frame->slots = EC->stack;
    frame->isCCall = false;
    frame->callLine = 1;
    frame->file = hiddenString("file", 4);
    // catch all errors
    void *res = vm_protect(raiseErrProtect, &arg, NULL, &status);
    T_ASSERT_EQ(TAG_RAISE, status);
    T_ASSERT_EQ(NULL, res);
    T_ASSERT_EQ(false, THREAD()->hadError);
    T_ASSERT(THREAD()->errInfo != NULL);
    popFrame();
    T_ASSERT_EQ(NULL, THREAD()->errInfo); // frame popped, errInfo for frame should be gone
cleanup:
    freeVM();
    return 0;
}

static int test_vm_protect3(void) {
    initVM();
    int arg = 4;
    ErrTag status = TAG_NONE;
    EC->frameCount = 0;
    CallFrame *frame = pushFrame();
    frame->start = 0;
    frame->ip = 0;
    frame->slots = EC->stack;
    frame->isCCall = false;
    frame->callLine = 1;
    frame->file = hiddenString("file", 4);
    void *res = vm_protect(raiseNoErrProtect, &arg, NULL, &status);
    T_ASSERT_EQ(TAG_NONE, status);
    T_ASSERT_EQ(lxAryClass, res);
    T_ASSERT_EQ(false, THREAD()->hadError);
    T_ASSERT_EQ(NULL, THREAD()->errInfo); // due to no error thrown, the errinfo should be gone
    popFrame();
cleanup:
    freeVM();
    return 0;
}

static int test_addition(void) {
    char *src = "1+1;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_NUMBER(*val));
    T_ASSERT_EQ(2.0, AS_NUMBER(*val));
cleanup:
    freeVM();
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
    freeVM();
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
    freeVM();
    return 0;
}

static int test_print_number(void) {
    char *src = "print 2.0;";
    interp(src, true);
cleanup:
    freeVM();
    return 0;
}

static int test_print_string(void) {
    char *src = "print \"howdy\";";
    interp(src, true);
cleanup:
    freeVM();
    return 0;
}

static int test_global_vars1(void) {
    char *src = "var greet = \"howdy\";"
                "greet;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_A_STRING(*val));
    T_ASSERT_STREQ("howdy", INSTANCE_AS_CSTRING(*val));
cleanup:
    freeVM();
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
    freeVM();
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
    freeVM();
    return 0;
}

static int test_simple_if(void) {
    char *src = "if (false) { print(\"woops\"); \"woops\"; } else { print \"jumped\"; \"jumped\"; }";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_A_STRING(*val));
    T_ASSERT_STREQ("jumped", INSTANCE_AS_CSTRING(*val));
cleanup:
    freeVM();
    return 0;
}

static int test_vardecls_in_block_not_global(void) {
    char *src = "var a = \"outer\"; if (true) { var a = \"in block\"; a; }";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_A_STRING(*val));
    T_ASSERT_STREQ("in block", INSTANCE_AS_CSTRING(*val));
cleanup:
    freeVM();
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
    freeVM();
    return 0;
}

static int test_simple_function(void) {
    char *src = "fun f() { return \"FUN\"; } var ret = f(); ret;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    /*fprintf(stderr, "typeof: %s", typeOfVal(*val));*/
    T_ASSERT(IS_A_STRING(*val));
    T_ASSERT_STREQ("FUN", INSTANCE_AS_CSTRING(*val));
cleanup:
    freeVM();
    return 0;
}

static int test_simple_class(void) {
    char *src = "class Train {} var t = Train(); print t; t;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_INSTANCE(*val));
    T_ASSERT_VALPRINTEQ("<instance Train>", *val);
    ObjInstance *inst = AS_INSTANCE(*val);
    Value objClassVal = OBJ_VAL(CLASSINFO(inst->klass)->superclass);
    T_ASSERT_VALPRINTEQ("<class Object>", objClassVal);
cleanup:
    freeVM();
    return 0;
}

static int test_simple_class_initializer(void) {
    char *src = "class Train {\n"
                " init(color) {\n"
                "    this.color = color;\n"
                "  }\n"
                "}\n"
                "var t = Train(\"Red\");\n"
                "print t.color;\n"
                "t.color;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_A_STRING(*val));
    /*T_ASSERT_VALPRINTEQ("Red", *val);*/
cleanup:
    freeVM();
    return 0;
}

static int test_simple_class_initializer2(void) {
    char *src = "class Train {\n"
                "  init(color) {\n"
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
    freeVM();
    return 0;
}

static int test_simple_subclass(void) {
    char *src = "class Train < Object {\n"
                "  init(color) {\n"
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
    freeVM();
    return 0;
}

static int test_simple_method1(void) {
    char *src = "class Train {\n"
                "  choo() { print \"choo\"; return this; }\n"
                "}\n"
                "var t = Train();\n"
                "t.choo().choo();\n";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(val != NULL);
    T_ASSERT(IS_INSTANCE(*val));
cleanup:
    freeVM();
    return 0;
}

static int test_native_clock(void) {
    char *src = "print clock(); clock();";
    interp(src, true);
    Value *val = getLastValue();
    printVMStack(stderr, THREAD());
    T_ASSERT(val != NULL);
    T_ASSERT(IS_NUMBER(*val));
cleanup:
    freeVM();
    return 0;
}

static int test_native_clock_bad_args(void) {
    char *src = "print clock(\"uh oh\");";
    InterpretResult ires = interp(src, false);
    T_ASSERT_EQ(INTERPRET_RUNTIME_ERROR, ires);
cleanup:
    freeVM();
    return 0;
}

static int test_throw_catch1(void) {
    char *src = "class MyError < Error { }\n"
                "try {\n"
                "  print \"throwing\";\n"
                "  throw MyError();\n"
                "  print \"shouldn't get here!!\";\n"
                "} catch (MyError e) {\n"
                "  print e;\n"
                "  e;\n"
                "}";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT_VALPRINTEQ("<instance MyError>", *val);
cleanup:
    freeVM();
    return 0;
}

static int test_throw_catch2(void) {
    char *src = "class MyError < Error { }\n"
                "class MyError2 < Error { }\n"
                "try {\n"
                "  print \"throwing\";\n"
                "  throw MyError();\n"
                "  print \"shouldn't get here!!\";\n"
                "} catch (MyError2 e) {\n"
                "  print e;\n"
                "  e;\n"
                "} catch (MyError e) {\n"
                "  print e;\n"
                "  e;\n"
                "}\n";

    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT_VALPRINTEQ("<instance MyError>", *val);
cleanup:
    freeVM();
    return 0;
}

static int test_throw_catch_across_function_boundaries(void) {
    char *src = "class MyError < Error { }\n"
                "fun doThrow() {\n"
                "  throw MyError();\n"
                "}\n"
                "try {\n"
                "  doThrow();"
                "} catch (MyError e) {\n"
                "  print e;\n"
                "  e;\n"
                "}\n";

    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT_VALPRINTEQ("<instance MyError>", *val);
cleanup:
    freeVM();
    return 0;
}

static int test_throw_catch_across_function_boundaries2(void) {
    char *src = "class MyError < Error { }\n"
                "fun doThrow() {\n"
                "  throw MyError();\n"
                "}\n"
                "doThrow();"
                "try {\n"
                "} catch (MyError e) {\n"
                "  print e;\n"
                "  e;\n"
                "}\n";

    InterpretResult ires = interp(src, false);
    T_ASSERT_EQ(INTERPRET_RUNTIME_ERROR, ires);
cleanup:
    freeVM();
    return 0;
}

static int test_throw_catch_across_function_boundaries3(void) {
    char *src = "class MyError < Error { }\n"
                "fun doThrow() {\n"
                "  throw MyError();\n"
                "}\n"
                "try {\n"
                "  print nil;\n"
                "} catch (MyError e) {\n"
                "  print e;\n"
                "  e;\n"
                "}\n"
                "doThrow();";

    InterpretResult ires = interp(src, false);
    T_ASSERT_EQ(INTERPRET_RUNTIME_ERROR, ires);
cleanup:
    freeVM();
    return 0;
}

static int test_get_set_arbitrary_property() {
    char *src = "class MyPet { }\n"
                "var p = MyPet();\n"
                "p.name = \"Gracie\";\n"
                "p.name;";
    interp(src, true);
    Value *val = getLastValue();
    printValue(stderr, *val, false, -1);
    T_ASSERT(IS_A_STRING(*val));
    T_ASSERT_STREQ("Gracie", INSTANCE_AS_CSTRING(*val));
cleanup:
    freeVM();
    return 0;
}

static int test_short_circuit_and() {
    char *src = "var b = nil;\n"
                "fun test() { b = false; return true; }\n"
                "var f = false and test();"
                "print f;\n"
                "b;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(IS_NIL(*val));
    char *src2 = "var b = nil;\n"
                 "fun test() { b = false; return true; }\n"
                 "test();\n"
                 "var f = true and test();\n"
                 "print f;\n"
                 "b;";

    freeVM();
    interp(src2, true);
    val = getLastValue();
    T_ASSERT(IS_BOOL(*val));
    T_ASSERT_EQ(false, AS_BOOL(*val));
cleanup:
    freeVM();
    return 0;
}

static int test_short_circuit_or() {
    char *src = "var b = nil;\n"
                "fun test() { b = false; return true; }\n"
                "var f = true or test();\n" // skips RHS
                "print f;\n"
                "b;";
    interp(src, true);
    Value *val = getLastValue();
    T_ASSERT(IS_NIL(*val));
    char *src2 = "var b = nil;\n"
                 "fun test() { b = false; return true; }\n"
                 "var f = false or test();\n" // eval RHS
                 "print f;\n"
                 "b;";
    freeVM();
    interp(src2, true);
    val = getLastValue();
    T_ASSERT(IS_BOOL(*val));
    T_ASSERT_EQ(false, AS_BOOL(*val));
cleanup:
    freeVM();
    return 0;
}

static int test_native_typeof() {
    char *src = "class MyPet { }\n"
                "var p = MyPet();\n"
                "print typeof(p);\n"
                "print typeof(nil);\n"
                "print typeof(true);\n"
                "print typeof(false);\n"
                "print typeof(1);\n"
                "print typeof(1.0);\n"
                "print typeof(\"str\");\n"
                "print typeof(MyPet);\n";
                /*"print typeof([])\n""*/
    initVM();
    ObjString *buf = copyString("", 0);
    setPrintBuf(buf, false);
    interp(src, true);
    char *output = buf->chars;
    ASSERT(buf->chars);
    char *expected = "instance\n"
                     "nil\n"
                     "bool\n"
                     "bool\n"
                     "number\n"
                     "number\n"
                     "string\n"
                     "class\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    unsetPrintBuf();
    freeVM();
    return 0;
}

static int test_array_literal() {
    char *src = "var a = [1,2,3]; print a.toString(); a;";
    initVM();
    ObjString *buf = copyString("", 0);
    setPrintBuf(buf, false);
    interp(src, true);
    Value *val = getLastValue();
    ASSERT(val);
    T_ASSERT(IS_T_ARRAY(*val));
    char *output = buf->chars;
    ASSERT(buf->chars);
    char *expected = "[1,2,3]\n";
    T_ASSERT_STREQ(expected, output);
cleanup:
    unsetPrintBuf();
    freeVM();
    return 0;
}

static int test_while_loop_stack() {
    char *src = "var i = 0;\n"
                "while (i < 300) {\n"
                "  print i;\n"
                "  i = i+1;\n"
                "}";
    interp(src, true);
    fprintf(stderr, "stackframes: %d\n", VMNumStackFrames());
    T_ASSERT_EQ(0, VMNumStackFrames());
cleanup:
    freeVM();
    return 0;
}

static int test_array_get_set() {
    char *src = "var a = [1,2,3];\n"
                "a[0] = 400;\n"
                "print a[0]; print a.toString();";
    initVM();
    ObjString *buf = copyString("", 0);
    setPrintBuf(buf, false);
    interp(src, true);
    ASSERT(buf->chars);
    const char *expected = "400\n"
                           "[400,2,3]\n";
    T_ASSERT_STREQ(expected, buf->chars);
cleanup:
    unsetPrintBuf();
    freeVM();
    return 0;
}

static int test_print_nested_array(void) {
    char *src = "var a = [[4],1,2,3];\n"
                "print a; print a.toString();";
    initVM();
    ObjString *buf = copyString("", 0);
    setPrintBuf(buf, false);
    interp(src, true);
    ASSERT(buf->chars);
    const char *expected = "[[4],1,2,3]\n"
                           "[[4],1,2,3]\n";

    T_ASSERT_STREQ(expected, buf->chars);
cleanup:
    unsetPrintBuf();
    freeVM();
    return 0;
}

static int test_print_map(void) {
    char *src = "var m = Map();\n"
                "print m;";
    initVM();
    ObjString *buf = copyString("", 0);
    setPrintBuf(buf, false);
    interp(src, true);
    const char *expected = "{}\n";
    ASSERT(buf->chars);
    T_ASSERT_STREQ(expected, buf->chars);
    unsetPrintBuf();
    freeVM();

    initVM();
    buf = copyString("", 0);
    setPrintBuf(buf, false);
    src =  "var m2 = Map(); m2[1] = 2; m2[2] = 4; print m2;";
    interp(src, true);
    expected = "{1 => 2, 2 => 4}\n";
    ASSERT(buf->chars);
    T_ASSERT_STREQ(expected, buf->chars);
cleanup:
    unsetPrintBuf();
    freeVM();
    return 0;
}

static int test_closures_global_scope(void) {
    char *src = "var i = 0;\n"
                "fun incr() { i = i + 1; print i; }\n"
                "print i;\n"
                "incr(); incr();\n"
                "print i + 1;";
    initVM();
    ObjString *buf = copyString("", 0);
    setPrintBuf(buf, false);
    interp(src, true);
    const char *expected = "0\n1\n2\n3\n";
    T_ASSERT_STREQ(expected, buf->chars);
cleanup:
    unsetPrintBuf();
    freeVM();
    return 0;
}

static int test_closures_env_saved(void) {
    char *src = "var i = 10;\n"
                "fun adder(a) { return fun(b) { return a+b; }; }\n"
                "var add10 = adder(i);\n"
                "print add10(20);\n"
                "print add10(40);\n";
    initVM();
    ObjString *buf = copyString("", 0);
    setPrintBuf(buf, false);
    interp(src, true);
    const char *expected = "30\n50\n";
    T_ASSERT_STREQ(expected, buf->chars);
    T_ASSERT_EQ(0, VMNumStackFrames());
    T_ASSERT_EQ(NULL, THREAD()->openUpvalues);
cleanup:
    unsetPrintBuf();
    freeVM();
    return 0;
}

static int test_catch_thrown_errors_from_c_code(void) {
    char *src = "try {\n"
                "  var m = Map(1, 2, 3, 4, 5);\n" // invalid constructor call
                "} catch (Error e) {\n"
                "  print \"caught\";\n"
                "}";
    initVM();
    ObjString *buf = copyString("", 0);
    setPrintBuf(buf, false);
    interp(src, true);
    const char *expected = "caught\n";
    T_ASSERT_STREQ(expected, buf->chars);
cleanup:
    unsetPrintBuf();
    freeVM();
    return 0;
}

static int test_map_keys_work_as_expected(void) {
    char *src = "var m = Map();\n"
                "m[10] = 10;\n"
                "m['10'] = 5;\n"
                "m['10'] = m['10']+1;\n"
                "m[10] = 9;\n"
                "print m[10];\n"
                "print m['10'];\n";
    initVM();
    ObjString *buf = copyString("", 0);
    setPrintBuf(buf, false);
    interp(src, true);
    const char *expected = "9\n6\n";
    T_ASSERT_STREQ(expected, buf->chars);
cleanup:
    unsetPrintBuf();
    freeVM();
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    compilerOpts.noRemoveUnusedExpressions = true;
    initSighandlers();

    INIT_TESTS();
    REGISTER_T_ASSERT_ON_FAIL(freeVM);
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
    RUN_TEST(test_simple_subclass);
    RUN_TEST(test_simple_method1);
    RUN_TEST(test_native_clock);
    RUN_TEST(test_native_clock_bad_args);
    RUN_TEST(test_throw_catch1);
    RUN_TEST(test_throw_catch2);
    RUN_TEST(test_throw_catch_across_function_boundaries);
    RUN_TEST(test_throw_catch_across_function_boundaries2);
    RUN_TEST(test_throw_catch_across_function_boundaries3);
    RUN_TEST(test_get_set_arbitrary_property);
    RUN_TEST(test_short_circuit_and);
    RUN_TEST(test_short_circuit_or);
    RUN_TEST(test_native_typeof);
    RUN_TEST(test_array_literal);
    RUN_TEST(test_while_loop_stack);
    RUN_TEST(test_array_get_set);
    RUN_TEST(test_print_nested_array);
    RUN_TEST(test_print_map);
    RUN_TEST(test_closures_global_scope);
    RUN_TEST(test_closures_env_saved);
    RUN_TEST(test_catch_thrown_errors_from_c_code);
    RUN_TEST(test_map_keys_work_as_expected);
    RUN_TEST(test_vm_protect1);
    RUN_TEST(test_vm_protect2);
    RUN_TEST(test_vm_protect3);
    END_TESTS();
}
