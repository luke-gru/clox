#ifndef clox_chunk_h
#define clox_chunk_h

#include "value.h"
#include "vec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CatchTable {
    // Row info
    int ifrom; // instruction try start
    int ito; // instruction try end
    int itarget; // instruction try catch start
    Value catchVal; // catch class or other value
    Value lastThrownValue; // runtime VM value of last thrown instance

    struct CatchTable *next; // next row in the catch table
} CatchTable;

/**
 * Chunk of bytecode, along with lines that they originated from in
 * the source code, and the string and number constants the bytecode
 * uses. Also features a catch table. This is all per function information,
 * as chunks are unique per source code function context, including the
 * top-level (main) function.
 */
typedef struct Chunk {
    int count; // number of bytes written to chunk
    int capacity;
    uint8_t *code; // bytecode written to chunk
    // parallel array with `code`, ex: byte chunk->code[i] comes from line
    // chunk->lines[i]
    int *lines; // lineno associated with bytecode instruction
    int *ndepths; // node depth level associated with bytecode instruction (used by debugger)
    int *nwidths; // node width level ...
    ValueArray *constants;
    CatchTable *catchTbl;
} Chunk;

typedef struct NodeLvl {
    int depth;
    int width;
} NodeLvl;

#define MAX_INSN_SIZE 4
#define MAX_INSN_OPERANDS (MAX_INSN_SIZE-1)
#define INSN_FL_NUMBER 1
#define INSN_FL_BREAK 2
// single instruction
typedef struct Insn {
    uint8_t code;
    uint8_t operands[MAX_INSN_OPERANDS];
    int numOperands;
    int lineno;
    unsigned flags;
    struct Insn *next;
    struct Insn *prev;
    struct Insn *jumpTo; // for jump instructions
    struct Insn *jumpedFrom; // for labels
    NodeLvl nlvl;
    bool isLabel; // is this a jump target?
    bool isJumpLabel;
    bool isLoopLabel; // is this a jump target for OP_LOOP?
    bool isPseudo; // pseudo-instruction
    int jumpNo; // for jump instructions
    int loopNo; // for loop instructions
} Insn;

// Instruction sequence for a single function (or top-level).
// This format is easier to manipulate for bytecode optimization than
// the chunk format (which is a linear array of bytes), so the compiler generates
// Iseqs, then at the end compiles them into Chunks.
typedef struct Iseq {
    int count; // # of Insns
    int byteCount;
    ValueArray *constants;
    CatchTable *catchTbl;
    Insn *tail; // tail of insns list
    Insn *insns; // head of doubly linked list of insns
} Iseq;

void initChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte, int lineno, int nodeDepth, int nodeWidth);
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

void initIseq(Iseq *seq);
void iseqAddInsn(Iseq *seq, Insn *toAdd);
bool iseqRmInsn(Iseq *seq, Insn *toRm);
void freeIseq(Iseq *seq);
int iseqAddConstant(Iseq *seq, Value value);
size_t iseqInsnByteDiff(Insn *prev, Insn *after);
int iseqInsnIndex(Iseq *seq, Insn *insn);
Insn *insnAtOffset(Iseq *seq, int offset);
int iseqAddCatchRow(
    Iseq *seq,
    int ifrom,
    int ito,
    int itarget,
    Value catchVal
);

// debugging
void debugInsn(Insn *insn);

#ifdef __cplusplus
}
#endif

#endif
