#include "mir.h"
#include "value.h"
#include "debug.h"
#include "memory.h"

static BasicBlock *curBlock;
static MirNode *curLabel;
static Iseq *curIseq;
uint16_t varNum = 1;
uint16_t labelNum = 1;

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

static uint16_t genLabelNum(void) {
    return labelNum++;
}

static Value getInsnValue(Insn *insn, uint8_t idx) {
    ASSERT(curIseq->constants->count > (int)idx);
    return curIseq->constants->values[idx];
}

static void mirAddNode(Mir *mir, MirNode *node) {
    vec_push(&mir->v_nodes, node);
}

static void mirPushStack(Mir *mir, MirNode *node) {
    vec_push(&mir->v_stack, node);
}

static MirNode *mirPeekStack(Mir *mir, uint8_t n) {
    ASSERT(mir->v_stack.length > (int)n);
    return mir->v_stack.data[mir->v_stack.length-1-(int)n];
}

static void mirPopStack(Mir *mir) {
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
    } else if (ty == MTY_LABEL) {
        node->labelNum = genLabelNum();
    }
    return node;
}

static void nodeAddOperand(MirNode *node, MirNode *op) {
    if (node->numOps == 0) {
        node->op1 = op;
    } else if (node->numOps == 1) {
        node->op2 = op;
    } else if (node->numOps == 2) {
        node->op3 = op;
    } else {
        ASSERT(0);
    }
    node->numOps++;
}

static void nodePushLabel(Mir *mir, MirNode *label) {
    vec_push(&mir->v_labels, label);
    curLabel = label;
}

static void pushNewBB(Mir *mir) {
    curBlock = ALLOCATE(BasicBlock, 1);
    vec_push(&mir->v_blocks, curBlock);
}

static void nodeSetLocal(Mir *mir, MirNode *node, uint8_t idx) {
    ASSERT(mir->v_locals.length >= (int)idx);
    if (mir->v_locals.length < ((int)idx+1)) {
        vec_push(&mir->v_locals, node);
    } else {
        mir->v_locals.data[idx] = node;
    }
}

static MirNode *nodeGetLocal(Mir *mir, int idx) {
    ASSERT(mir->v_locals.length > idx);
    return mir->v_locals.data[idx];
}

static void genMir_CONSTANT(Mir *mir, Insn *insn) {
    MirNode *def = createNode(MTY_DEFINITION);
    def->insn = insn;
    def->opcode = MOP_STORE_IMM;
    MirNode *val = createNode(MTY_IMM);
    val->value = getInsnValue(insn, insn->operands[0]);
    nodeAddOperand(def, val);
    mirAddNode(mir, def);
    mirPushStack(mir, def);
}

static void genMir_SET_LOCAL(Mir *mir, Insn *insn) {
    MirNode *def = createNode(MTY_DEFINITION);
    def->insn = insn;
    def->opcode = MOP_STORE;
    MirNode *val = mirPeekStack(mir, 0);
    nodeAddOperand(def, val);
    nodeSetLocal(mir, def, insn->operands[0]);
    mirAddNode(mir, def);
}

static void genMir_GET_LOCAL(Mir *mir, Insn *insn) {
    MirNode *def = createNode(MTY_DEFINITION);
    def->insn = insn;
    def->opcode = MOP_STORE;
    MirNode *val = nodeGetLocal(mir, insn->operands[0]);
    nodeAddOperand(def, val);
    mirAddNode(mir, def);
    mirPushStack(mir, def);
}

static void genMir_PRINT(Mir *mir, Insn *insn) {
    MirNode *def = createNode(MTY_INSTRUCTION);
    def->insn = insn;
    def->opcode = MOP_PRINT;
    MirNode *val = mirPeekStack(mir, 0);
    nodeAddOperand(def, val);
    mirAddNode(mir, def);
    mirPopStack(mir);
}

static void genMir_ADD(Mir *mir, Insn *insn) {
    MirNode *bin = createNode(MTY_INSTRUCTION);
    bin->insn = insn;
    bin->opcode = MOP_ADD;
    MirNode *binReg = createNode(MTY_DEFINITION);
    binReg->insn = insn;
    nodeAddOperand(bin, binReg);
    MirNode *val1 = mirPeekStack(mir, 1);
    MirNode *val2 = mirPeekStack(mir, 0);
    nodeAddOperand(bin, val1);
    nodeAddOperand(bin, val2);

    mirAddNode(mir, bin);
    mirPopStack(mir);
    mirPopStack(mir);
    mirPushStack(mir, binReg);
}

static void genMir_SUBTRACT(Mir *mir, Insn *insn) {
    MirNode *bin = createNode(MTY_INSTRUCTION);
    bin->insn = insn;
    bin->opcode = MOP_SUBTRACT;
    MirNode *binReg = createNode(MTY_DEFINITION);
    binReg->insn = insn;
    nodeAddOperand(bin, binReg);
    MirNode *val1 = mirPeekStack(mir, 1);
    MirNode *val2 = mirPeekStack(mir, 0);
    nodeAddOperand(bin, val1);
    nodeAddOperand(bin, val2);

    mirAddNode(mir, bin);
    mirPopStack(mir);
    mirPopStack(mir);
    mirPushStack(mir, binReg);
}

static void genMir_RETURN(Mir *mir, Insn *insn) {
    MirNode *ret = createNode(MTY_INSTRUCTION);
    ret->insn = insn;
    ret->opcode = MOP_RETURN;
    MirNode *val = mirPeekStack(mir, 0);
    nodeAddOperand(ret, val);
    mirAddNode(mir, ret);
    mirPopStack(mir);
}

static void genMir_JUMP_IF_FALSE(Mir *mir, Insn *insn) {
    MirNode *jmp = createNode(MTY_INSTRUCTION);
    jmp->insn = insn;
    jmp->opcode = MOP_JUMP_FALSE;
    MirNode *testReg = mirPeekStack(mir, 0);
    mirPopStack(mir);
    nodeAddOperand(jmp, testReg);
    MirNode *label = createNode(MTY_LABEL);
    label->insn = insn;
    nodeAddOperand(jmp, label);
    nodePushLabel(mir, label);
    label->labelOffset = insn->operands[0];
    mirAddNode(mir, jmp);
    pushNewBB(mir);
}

static void genMir_JUMP(Mir *mir, Insn *insn) {
    MirNode *jmp = createNode(MTY_INSTRUCTION);
    jmp->insn = insn;
    jmp->opcode = MOP_JUMP;
    MirNode *label = createNode(MTY_LABEL);
    label->insn = insn;
    nodeAddOperand(jmp, label);
    nodePushLabel(mir, label);
    label->labelOffset = insn->operands[0];
    mirAddNode(mir, jmp);
    pushNewBB(mir);
}

static void genMir_POP(Mir *mir, Insn *insn) {
    mirPopStack(mir);
}

static void genMir_LEAVE(Mir *mir, Insn *insn) {
    MirNode *leave = createNode(MTY_INSTRUCTION);
    leave->insn = insn;
    leave->opcode = MOP_LEAVE;
    mirAddNode(mir, leave);
}

static void genMirLabel(Mir *mir, Insn *insn) {
    MirNode *label = NULL;
    int labelIdx = 0;
    vec_foreach(&mir->v_labels, label, labelIdx) {
        size_t insnDiff = iseqInsnByteDiff(curIseq->insns, insn);
        size_t labelDiff = iseqInsnByteDiff(curIseq->insns, label->insn);
        if (insnDiff > labelDiff && (labelDiff+label->labelOffset) == insnDiff-1) {
            MirNode *labelNode = createNode(MTY_LABEL);
            labelNode->insn = insn;
            labelNode->opcode = MOP_LABEL;
            labelNode->labelNum = label->labelNum;
            mirAddNode(mir, labelNode);
        }
    }
}

static void genMirInsn(Mir *mir, Insn *insn) {
    genMirLabel(mir, insn);
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
    case OP_ADD:
        MIR_DEBUG(1, "gen add");
        genMir_ADD(mir, insn);
        break;
    case OP_SUBTRACT:
        MIR_DEBUG(1, "gen subtract");
        genMir_SUBTRACT(mir, insn);
        break;
    case OP_JUMP_IF_FALSE:
        MIR_DEBUG(1, "gen jump_if_false");
        genMir_JUMP_IF_FALSE(mir, insn);
        break;
    case OP_JUMP:
        MIR_DEBUG(1, "gen jump");
        genMir_JUMP(mir, insn);
        break;
    case OP_POP:
        MIR_DEBUG(1, "gen pop");
        genMir_POP(mir, insn);
        break;
    case OP_RETURN:
        MIR_DEBUG(1, "gen return");
        genMir_RETURN(mir, insn);
        break;
    case OP_LEAVE:
        MIR_DEBUG(1, "gen leave");
        genMir_LEAVE(mir, insn);
        break;
    default:
        fprintf(stderr, "Not yet implemented: op %s\n", opName((OpCode)insn->code));
        ASSERT(0);
    }
}

void initMir(Mir *mir) {
    vec_init(&mir->v_nodes);
    vec_init(&mir->v_stack);
    vec_init(&mir->v_locals);
    vec_init(&mir->v_labels);
    vec_init(&mir->v_blocks);
}


Mir genMir(Iseq *iseq) {
    DBG_ASSERT(iseq);
    curIseq = iseq;
    Mir mir;
    initMir(&mir);
    pushNewBB(&mir);
    // reinitialize statics
    varNum = 1;
    labelNum = 1;

    Insn *cur = iseq->insns;
    while (cur) {
        genMirInsn(&mir, cur);
        cur = cur->next;
    }
    ASSERT(mir.v_stack.length == 0);
    return mir;
}

static void dumpStoreImmNode(FILE *f, MirNode *n) {
    MirNode *valNode = n->op1;
    if (IS_NUMBER(valNode->value)) {
        double numVal = AS_NUMBER(valNode->value);
        fprintf(f, "storeImm v%d, %g\n", n->varNum, numVal);
    } else if (IS_CLOSURE(valNode->value)) {
        ObjClosure *closure = AS_CLOSURE(valNode->value);
        fprintf(f, "storeImm v%d, %p\n", n->varNum, closure);
    }
}

static void dumpStoreNode(FILE *f, MirNode *n) {
    MirNode *fromNode = n->op1;
    fprintf(f, "store v%d, v%d\n", n->varNum, fromNode->varNum);
}

static void dumpPrintNode(FILE *f, MirNode *n) {
    MirNode *fromNode = n->op1;
    fprintf(f, "print v%d\n", fromNode->varNum);
}

static void dumpAddNode(FILE *f, MirNode *n) {
    MirNode *storeReg = n->op1;
    MirNode *val1Reg = n->op2;
    MirNode *val2Reg = n->op3;
    fprintf(f, "add v%d, v%d, v%d\n", storeReg->varNum,
            val1Reg->varNum, val2Reg->varNum);
}

static void dumpSubtractNode(FILE *f, MirNode *n) {
    MirNode *storeReg = n->op1;
    MirNode *val1Reg = n->op2;
    MirNode *val2Reg = n->op3;
    fprintf(f, "sub v%d, v%d, v%d\n", storeReg->varNum,
            val1Reg->varNum, val2Reg->varNum);
}

static void dumpJumpFalseNode(FILE *f, MirNode *n) {
    MirNode *testReg = n->op1;
    MirNode *label = n->op2;
    fprintf(f, "jumpfalse v%d, label%d\n", testReg->varNum,
            label->labelNum);
}

static void dumpJumpNode(FILE *f, MirNode *n) {
    MirNode *label = n->op1;
    fprintf(f, "jump label%d\n", label->labelNum);
}

static void dumpReturnNode(FILE *f, MirNode *n) {
    fprintf(f, "return v%d\n", n->op1->varNum);
}

static void dumpLeaveNode(FILE *f, MirNode *n) {
    fprintf(f, "leave\n");
}

static void dumpLabelNode(FILE *f, MirNode *n) {
    fprintf(f, "label%d:\n", n->labelNum);
}

static void dumpMirNode(FILE *f, Mir *mir, MirNode *n) {
    switch (n->opcode) {
    case MOP_STORE_IMM:
        dumpStoreImmNode(f, n);
        break;
    case MOP_STORE:
        dumpStoreNode(f, n);
        break;
    case MOP_PRINT:
        dumpPrintNode(f, n);
        break;
    case MOP_ADD:
        dumpAddNode(f, n);
        break;
    case MOP_SUBTRACT:
        dumpSubtractNode(f, n);
        break;
    case MOP_JUMP_FALSE:
        dumpJumpFalseNode(f, n);
        break;
    case MOP_JUMP:
        dumpJumpNode(f, n);
        break;
    case MOP_RETURN:
        dumpReturnNode(f, n);
        break;
    case MOP_LEAVE:
        dumpLeaveNode(f, n);
        break;
    case MOP_LABEL:
        dumpLabelNode(f, n);
        break;
    default:
        ASSERT(0);
        break;
    }
}

void dumpMir(FILE *f, Mir mir) {
    MirNode *mirNode = NULL;
    int nodeIdx = 0;
    vec_foreach(&mir.v_nodes, mirNode, nodeIdx) {
        dumpMirNode(f, &mir, mirNode);
    }
}
