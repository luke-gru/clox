#include <stdio.h>
#include "mir.h"
#include "value.h"
#include "debug.h"
#include "memory.h"

static BasicBlock *curBlock;
static Iseq *curIseq;
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

static Value getInsnValue(Insn *insn, uint8_t idx) {
    ASSERT(curIseq->constants->count > (int)idx);
    return curIseq->constants->values[idx];
}

static void mirAddNode(Mir *mir, MirNode *node) {
    vec_push(&mir->v_nodes, node);
}

static void mirAddStack(Mir *mir, MirNode *node) {
    vec_push(&mir->v_stack, node);
}

static MirNode *peekStack(Mir *mir, uint8_t n) {
    ASSERT(mir->v_stack.length > (int)n);
    return mir->v_stack.data[mir->v_stack.length-1-(int)n];
}

static void popStack(Mir *mir) {
    ASSERT(mir->v_stack.length > 0);
    (void)vec_pop(&mir->v_stack);
}

static MirNode *createNode(MirNodeType ty) {
    MirNode *node = ALLOCATE(MirNode, 1);
    memset(node, 0, sizeof(MirNode));
    DBG_ASSERT(curBlock);
    node->ty = ty;
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

static void nodeSetLocal(Mir *mir, MirNode *node, uint8_t idx) {
    ASSERT(mir->v_locals.length >= (int)idx);
    if (mir->v_locals.length < ((int)idx+1)) {
        vec_push(&mir->v_locals, node);
    } else {
        mir->v_locals.data[idx] = node;
    }
}

static MirNode *nodeGetLocal(Mir *mir, unsigned idx) {
    ASSERT(mir->v_locals.length > idx);
    return mir->v_locals.data[idx];
}

static void genMir_CONSTANT(Mir *mir, Insn *insn) {
    MirNode *def = createNode(MTY_DEFINITION);
    def->opcode = MOP_STORE_IMM;
    MirNode *val = createNode(MTY_IMM);
    val->value = getInsnValue(insn, insn->operands[0]);
    nodeAddOperand(def, val);
    mirAddNode(mir, def);
    mirAddStack(mir, def);
}

static void genMir_SET_LOCAL(Mir *mir, Insn *insn) {
    MirNode *def = createNode(MTY_DEFINITION);
    def->opcode = MOP_STORE;
    MirNode *val = peekStack(mir, 0);
    nodeAddOperand(def, val);
    nodeSetLocal(mir, def, insn->operands[0]);
    mirAddNode(mir, def);
}

static void genMir_GET_LOCAL(Mir *mir, Insn *insn) {
    MirNode *def = createNode(MTY_DEFINITION);
    def->opcode = MOP_STORE;
    MirNode *val = nodeGetLocal(mir, insn->operands[0]);
    nodeAddOperand(def, val);
    mirAddNode(mir, def);
    mirAddStack(mir, def);
}

static void genMir_PRINT(Mir *mir, Insn *insn) {
    MirNode *def = createNode(MTY_INSTRUCTION);
    def->opcode = MOP_PRINT;
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
        MIR_DEBUG(1, "gen constant");
        genMir_CONSTANT(mir, insn);
        break;
    case OP_SET_LOCAL:
        MIR_DEBUG(1, "gen set local");
        genMir_SET_LOCAL(mir, insn);
        break;
    case OP_GET_LOCAL:
        MIR_DEBUG(1, "gen get local");
        genMir_GET_LOCAL(mir, insn);
        break;
    case OP_PRINT:
        MIR_DEBUG(1, "gen print");
        genMir_PRINT(mir, insn);
        break;
    case OP_POP:
        MIR_DEBUG(1, "gen pop");
        genMir_POP(mir, insn);
        break;
    case OP_LEAVE:
        MIR_DEBUG(1, "gen leave");
        break;
    default:
        fprintf(stderr, "Not yet implemented: op %s\n", opName((OpCode)insn->code));
        ASSERT(0);
    }
}

Mir genMir(Iseq *iseq) {
    DBG_ASSERT(iseq);
    curBlock = ALLOCATE(BasicBlock, 1);
    curIseq = iseq;
    varNum = 1;
    Mir mir;
    vec_init(&mir.v_nodes);
    vec_init(&mir.v_stack);

    Insn *cur = iseq->insns;
    while (cur) {
        genMirInsn(&mir, cur);
        cur = cur->next;
    }
    ASSERT(mir.v_stack.length == 0);
    return mir;
}

static void emitStoreImmNode(MirNode *n) {
    MirNode *valNode = n->op1;
    double numVal = AS_NUMBER(valNode->value);
    fprintf(stderr, "storeImm v%d, %g\n", n->varNum, numVal);
}

static void emitStoreNode(MirNode *n) {
    MirNode *fromNode = n->op1;
    fprintf(stderr, "store v%d, v%d\n", n->varNum, fromNode->varNum);
}

static void emitPrintNode(MirNode *n) {
    MirNode *fromNode = n->op1;
    fprintf(stderr, "print v%d\n", fromNode->varNum);
}

static void emitMirNode(MirNode *n) {
    switch (n->opcode) {
    case MOP_STORE_IMM:
        emitStoreImmNode(n);
        break;
    case MOP_STORE:
        emitStoreNode(n);
        break;
    case MOP_PRINT:
        emitPrintNode(n);
        break;
    default:
        ASSERT(0);
        break;
    }
}

void emitMir(Mir mir) {
    MirNode *mirNode = NULL;
    int nodeIdx = 0;
    vec_foreach(&mir.v_nodes, mirNode, nodeIdx) {
        emitMirNode(mirNode);
    }
}
