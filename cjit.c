#include "cjit.h"
#include "vm.h"

#define JIT_READ_BYTE() ((*ip++))
#define JIT_READ_CONSTANT() (constantSlots[JIT_READ_BYTE()])
#define JIT_PUSH(val) push(val)
#define JIT_POP() pop()
#define JIT_NATIVE_SUCCESS 1
#define JIT_NATIVE_ERROR -1

static int jitEmit_CONSTANT(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  Value constant = JIT_READ_CONSTANT();\n");
    fprintf(f, "  JIT_PUSH(constant);\n");
    fprintf(f, "}\n");
    return 0;
}
static int jitEmit_ADD(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SUBTRACT(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_MULTIPLY(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_DIVIDE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_MODULO(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_BITOR(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_BITAND(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_BITXOR(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SHOVEL_L(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SHOVEL_R(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_NEGATE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_NOT(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_GET_LOCAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SET_LOCAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_UNPACK_SET_LOCAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_GLOBAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SET_GLOBAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_DEFINE_GLOBAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_CONST(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SET_CONST(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_CONST_UNDER(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_CLOSURE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_UPVALUE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SET_UPVALUE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_CLOSE_UPVALUE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_PROP_GET(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_PROP_SET(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_METHOD(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_CLASS_METHOD(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GETTER(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SETTER(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_CALL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_INVOKE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SPLAT_ARRAY(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_THIS(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_SUPER(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_RETURN(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_PRINT(FILE *f, Insn *insn) {
    fprintf(f, "{\n");
    fprintf(f, "  Value val = JIT_POP();\n");
    fprintf(f, "  printValue(stdout, val, true, -1);\n");
    fprintf(f, "  printf(\"\\n\");\n");
    fprintf(f, "}\n");
    return 0;
}

static int jitEmit_STRING(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_ARRAY(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_DUPARRAY(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_DUPMAP(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_MAP(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_REGEX(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_ITER(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_ITER_NEXT(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_BLOCK_BREAK(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_BLOCK_CONTINUE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_BLOCK_RETURN(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_TO_BLOCK(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_TRUE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_FALSE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_NIL(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_AND(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_OR(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_POP(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_POP_CREF(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_POP_N(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_EQUAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_NOT_EQUAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GREATER(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_LESS(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GREATER_EQUAL(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_LESS_EQUAL(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_JUMP(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_JUMP_IF_FALSE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_JUMP_IF_TRUE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_JUMP_IF_FALSE_PEEK(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_JUMP_IF_TRUE_PEEK(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_LOOP(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_CLASS(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_SUBCLASS(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_MODULE(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_IN(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_THROW(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_GET_THROWN(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_INDEX_GET(FILE *f, Insn *insn) {
    return 0;
}
static int jitEmit_INDEX_SET(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_CHECK_KEYWORD(FILE *f, Insn *insn) {
    return 0;
}

static int jitEmit_LEAVE(FILE *f, Insn *insn) {
    fprintf(f, ""
    "if (th == vm.mainThread && !isInEval() && !isInLoadedScript()) {\n"
    "  vm.exited = true;\n"
    "}\n"
    "(th->vmRunLvl)--;\n");
    return 0;
}


static int jitEmitInsn(FILE *f, Insn *insn) {
#define OPCODE(code) case OP_##code : return jitEmit_##code(f, insn);
    switch (insn->code) {
#include "opcodes.h.inc"
    default:
        fprintf(stderr, "Unknown instruction to jit: %d\n", insn->code);
        return -1;

    }
#undef OPCODE
}

static int jitEmitFunctionEnter(FILE *f, Iseq *seq, Node *funcNode) {
    fprintf(f, "int jittedFunc(LxThread *th, Value **sp, uint8_t **ip, Value *constantSlots) {\n");
    return 0;
}

static int jitEmitFunctionLeave(FILE *f, Iseq *seq, Node *funcNode) {
    fprintf(f, "return JIT_NATIVE_SUCCESS;\n");
    fprintf(f, "}\n");
    return 0;
}

FILE *jitEmitIseqFile(Iseq *seq, Node *funcNode) {
    FILE *f = fopen("/tmp/loxjit.c", "w");
    jitEmitIseq(f, seq, funcNode);
    fclose(f);
    return f;
}

int jitEmitIseq(FILE *f, Iseq *seq, Node *funcNode) {
    int ret = 0;
    jitEmitFunctionEnter(f, seq, funcNode);
    Insn *insn = seq->insns;
    while (insn) {
        if ((ret = jitEmitInsn(f, insn)) != 0) {
            return ret;
        }
        insn = insn->next;
    }
    jitEmitFunctionLeave(f, seq, funcNode);
    return ret;
}
