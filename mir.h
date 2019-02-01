#ifndef clox_mir_h
#define clox_mir_h

#include "chunk.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
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
 *
 * mir code:
 *
 * storeImm v1, 1 (v1 <- 1)
 * store v2, v1 (v2 <- v1)
 * store v3, v2 (v3 <- v2)
 * print v3
 *
 */

typedef enum MirOp {
    MOP_STORE_IMM,
    MOP_STORE,
    MOP_LOAD,
    MOP_PRINT,
} MirOp;

typedef enum MirNodeType {
    MTY_DEFINITION,
    MTY_INSTRUCTION,
    MTY_IMM,
} MirNodeType;

struct BasicBlock;

typedef struct MirNode {
    MirOp opcode;
    struct MirNode *op1;
    struct MirNode *op2;
    struct BasicBlock *block;
    Value value;
    uint16_t varNum;
    uint8_t numOps;
} MirNode;

typedef struct BasicBlock {
    vec_void_t v_nodes;
} BasicBlock;

typedef struct Mir {
    vec_void_t v_nodes;
    vec_void_t v_stack;
} Mir;

Mir genMir(Iseq *iseq);

#ifdef __cplusplus
}
#endif

#endif
