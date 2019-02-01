#include <stdio.h>
#include "mir.h"
#include "value.h"
#include "debug.h"
#include "memory.h"

static BasicBlock *curBlock;
uint16_t varNum = 1;

static void mir_debug(int lvl, const char *fmt, ...) {
    /*if (GET_OPTION(debugMir) < lvl) return;*/
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[MIR]: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

#ifdef NDEBUG
#define MIR_DEBUG(lvl, ...) (void)0
#else
#define MIR_DEBUG(lvl, ...) mir_debug(lvl, __VA_ARGS__)
#endif

static uint16_t genVarNum(void) {
    return varNum++;
}

static Value getInsnValue(Insn *insn, int idx) {
    return NUMBER_VAL(1); // TODO
}

static void mirAddNode(Mir *mir, MirNode *node) {
    vec_push(&mir->v_nodes, node);
}

static void mirAddStack(Mir *mir, MirNode *node) {
    vec_push(&mir->v_stack, node);
}

static MirNode *peekStack(Mir *mir, int n) {
    ASSERT(mir->v_stack.length > n);
    return mir->v_stack.data[n];
}

static void popStack(Mir *mir) {
    ASSERT(mir->v_stack.length > 0);
    (void)vec_pop(&mir->v_stack);
}

static MirNode *createNode(MirNodeType ty) {
    MirNode *node = ALLOCATE(MirNode, 1);
    memset(node, 0, sizeof(MirNode));
    DBG_ASSERT(curBlock);
    node->block = curBlock;
    if (ty == MTY_DEFINITION) {
        node->varNum = genVarNum();
    }
    return node;
}

static void nodeAddOperand(MirNode *node, MirNode *op) {
    node->op1 = op;
    node->numOps = 1;
}

static void genMir_CONSTANT(Mir *mir, Insn *insn) {
    MirNode *def = createNode(MTY_DEFINITION);
    MirNode *val = createNode(MTY_IMM);
    val->value = getInsnValue(insn, 0);
    nodeAddOperand(def, val);
    mirAddNode(mir, def);
    mirAddStack(mir, def);
}

static void genMir_SET_LOCAL(Mir *mir, Insn *insn) {
    MirNode *def = createNode(MTY_DEFINITION);
    MirNode *val = peekStack(mir, 0);
    nodeAddOperand(def, val);
    mirAddNode(mir, def);
}

static void genMir_GET_LOCAL(Mir *mir, Insn *insn) {
    MirNode *def = createNode(MTY_DEFINITION);
    MirNode *val = peekStack(mir, 0);
    nodeAddOperand(def, val);
    mirAddNode(mir, def);
    mirAddStack(mir, def);
}

static void genMir_PRINT(Mir *mir, Insn *insn) {
    MirNode *def = createNode(MTY_INSTRUCTION);
    MirNode *val = peekStack(mir, 0);
    nodeAddOperand(def, val);
    mirAddNode(mir, def);
    popStack(mir);
}

static void genMir_POP(Mir *mir, Insn *insn) {
    popStack(mir);
}

static void genMirInsn(Mir *mir, Insn *insn) {
    switch (insn->code) {
    case OP_CONSTANT:
        MIR_DEBUG(stderr, "gen constant");
        genMir_CONSTANT(mir, insn);
        break;
    case OP_SET_LOCAL:
        MIR_DEBUG(stderr, "gen set local");
        genMir_SET_LOCAL(mir, insn);
        break;
    case OP_GET_LOCAL:
        MIR_DEBUG(stderr, "gen get local");
        genMir_GET_LOCAL(mir, insn);
        break;
    case OP_PRINT:
        MIR_DEBUG(stderr, "gen print");
        genMir_PRINT(mir, insn);
        break;
    case OP_POP:
        MIR_DEBUG(stderr, "gen pop");
        genMir_POP(mir, insn);
        break;
    case OP_LEAVE:
        MIR_DEBUG(stderr, "gen leave");
        break;
    default:
        fprintf(stderr, "Not yet implemented: op %s\n", opName((OpCode)insn->code));
        ASSERT(0);
    }
}

Mir genMir(Iseq *iseq) {
    DBG_ASSERT(iseq);
    curBlock = ALLOCATE(BasicBlock, 1);
    varNum = 1;
    Mir mir;
    vec_init(&mir.v_nodes);
    vec_init(&mir.v_stack);

    Insn *cur = iseq->insns;
    while (cur) {
        genMirInsn(&mir, cur);
        cur = cur->next;
    }
    return mir;
}
