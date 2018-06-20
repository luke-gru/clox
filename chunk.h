#ifndef clox_chunk_h
#define clox_chunk_h

#include "value.h"
#include "vec.h"

typedef struct CatchTable CatchTable;
typedef struct CatchTable {
    // Row info
    int ifrom; // instruction try start
    int ito; // instruction try end
    int itarget; // instruction try catch start
    Value catchVal; // catch class or other value
    Value lastThrownValue; // runtime VM value of last thrown instance

    CatchTable *next; // next row in the catch table
} CatchTable;

/**
 * Chunk of bytecode, along with lines that they originated from in
 * the source code, and the string and number constants the bytecode
 * uses. Also features a catch table. This is all per function information,
 * as chunks are unique per source code function context, including the
 * top-level (main) function.
 */
typedef struct Chunk {
    int count;
    int capacity;
    uint8_t *code;
    // parallel array with `code`, ex: byte chunk->code[i] comes from line
    // chunk->lines[i]
    int *lines;
    ValueArray constants;
    CatchTable *catchTbl;
} Chunk;

//#define MAX_INSN_SIZE 3
//// single instruction
//typedef struct Insn {
    //uint8_t opcode;
    //uint8_t operands[MAX_INSN_SIZE-1];
    //int numOperands;
    //int lineno;
    //Insn *next;
//} Insn;

//// Instruction sequence for a single function (or top-level).
//// This format is easier to manipulate for bytecode optimization than
//// the chunk format (a linear array of bytes), so the compiler generates
//// Iseqs, then at the end compiles them into Chunks.
//typedef struct Iseq {
    //int count; // # of Insns
    //ValueArray constants;
    //CatchTable *catchTbl;
    //Insn *insns;
//} Iseq;

void initChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte, int lineno);
void freeChunk(Chunk *chunk);
int addConstant(Chunk *chunk, Value value);
Value getConstant(Chunk *chunk, int idx);
int addCatchRow(
    Chunk *chunk,
    int ifrom,
    int ito,
    int itarget,
    Value catchVal
);

#endif
