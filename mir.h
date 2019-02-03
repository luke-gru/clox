#ifndef clox_mir_h
#define clox_mir_h

#include <stdio.h>
#include "chunk.h"

#ifdef __cplusplus
extern "C" {
#endif
/* ex 1:
 * -----
 * {
 * var a = 1;
 * print a;
 * }
 *
 * bytecode:
 *
 * OP_CONSTANT 0 '1'
 * OP_SET_LOCAL 'a' [slot 0]
 * OP_GET_LOCAL 'a' [slot 0]
 * OP_PRINT
 * OP_POP
 * OP_LEAVE
 *
 * mir code:
 *
 * storeImm v1, 1 (v1 <- 1)
 * store v2, v1 (v2 <- v1)
 * store v3, v2 (v3 <- v2)
 * print v3
 * leave
 * -----
 *
 * ex 2:
 * -----
 * OP_CONSTANT 0 '1'
 * OP_SET_LOCAL 'a' [slot 0]
 * OP_GET_LOCAL 'a' [slot 0]
 * OP_JUMP_IF_FALSE (addr=16)
 * OP_GET_LOCAL 'a' [slot 0]
 * OP_PRINT
 * OP_JUMP (addr=19)
 * 16: OP_CONSTANT 1 '2'
 * OP_PRINT
 * 19: OP_POP
 * OP_LEAVE
 *
 * mir code:
 *
 * storeImm v1, 1 (v1 <- 1)
 * store v2, v1 (v2 <- v1)
 * store v3, v2 (v3 <- v2)
 * jumpfalse v3, label1
 * store v4, v2 (v4 <- v3)
 * print v4
 * jump label2
 * label1:
 * storeImm v5, 2
 * print v5
 * label2:
 * leave
 *
 */

typedef enum MirOp {
    MOP_STORE_IMM,
    MOP_STORE,
    MOP_LOAD,
    MOP_PRINT,
    MOP_JUMP_FALSE,
    MOP_JUMP,
    MOP_LEAVE,
    MOP_LABEL,
} MirOp;

typedef enum MirNodeType {
    MTY_DEFINITION,
    MTY_INSTRUCTION,
    MTY_IMM,
    MTY_LABEL,
} MirNodeType;

struct BasicBlock;

typedef struct MirNode {
    Insn *insn;
    MirNodeType ty;
    MirOp opcode;
    struct MirNode *op1;
    struct MirNode *op2;
    struct BasicBlock *block;
    Value value;
    uint16_t varNum;
    uint16_t labelNum;
    uint8_t numOps;
    uint8_t labelOffset;
} MirNode;

typedef struct BasicBlock {
    vec_void_t v_nodes;
} BasicBlock;

typedef struct Mir {
    vec_void_t v_nodes; // list of nodes
    vec_void_t v_stack; // op stack
    vec_void_t v_locals; // locals stack
    vec_void_t v_labels; // label stack
    vec_void_t v_blocks;
} Mir;

void initMir(Mir *mir);
Mir genMir(Iseq *iseq);

void dumpMir(FILE *f, Mir m);

#ifdef __cplusplus
}
#endif

#endif
