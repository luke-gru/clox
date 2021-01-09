#ifndef clox_chunk_h
#define clox_chunk_h

#include "value.h"
#include "vec.h"
#include "table.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t bytecode_t;
#define BYTECODE_MAX UINT32_MAX
#define BYTES_IN_INSTRUCTION 4

struct ObjString; // fwd decl

typedef struct CatchTable {
    // Row info
    int ifrom; // instruction try block start
    int ito; // instruction try block end
    int itarget; // instruction catch or ensure block start
    Value catchVal; // catch class or other value, if it's a catch block
    Value lastThrownValue; // runtime VM value of last thrown instance, if any. Otherwise it's NIL_VAL

    struct CatchTable *next; // next row in the catch table
    bool isEnsure; // is an ensure block
    bool isEnsureRunning;
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
    bytecode_t *code; // bytecode written to chunk
    // parallel array with `code`, ex: byte chunk->code[i] comes from line
    // chunk->lines[i]
    int *lines; // lineno associated with bytecode instruction
    int *ndepths; // node depth level associated with bytecode instruction (used by debugger)
    int *nwidths; // node width level ...
    ValueArray *constants;
    Table *varInfo; // for debugger, to print variables
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
#define INSN_FL_CONTINUE 4
// single instruction
typedef struct Insn {
    bytecode_t code;
    bytecode_t operands[MAX_INSN_OPERANDS];
    int numOperands;
    int lineno;
    unsigned flags;
    struct Insn *next;
    struct Insn *prev;
    struct Insn *jumpTo; // for jump instructions
    NodeLvl nlvl;
    bool isLabel; // is this a jump target?
} Insn;

// Instruction sequence for a single function (or top-level).
// This format is easier to manipulate for bytecode optimization than
// the chunk format (which is a linear array of bytes), so the compiler generates
// Iseqs, then at the end compiles them into Chunks.
typedef struct Iseq {
    int count; // # of Insns
    int wordCount;
    ValueArray *constants;
    CatchTable *catchTbl;
    Insn *tail; // tail of insns list
    Insn *insns; // head of doubly linked list of insns
} Iseq;

void initChunk(Chunk *chunk);
void writeChunkWord(Chunk *chunk, bytecode_t word, int lineno, int nodeDepth, int nodeWidth);
void freeChunk(Chunk *chunk);

int addConstant(Chunk *chunk, Value value);
Value getConstant(Chunk *chunk, int idx);
void addVarInfo(Chunk *chunk, struct ObjString *varName, int idx);
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
size_t iseqInsnWordDiff(Insn *prev, Insn *after);
int iseqInsnIndex(Iseq *seq, Insn *insn);
Insn *insnAtOffset(Iseq *seq, int offset);
int iseqAddCatchRow(
    Iseq *seq,
    int ifrom,
    int ito,
    int itarget,
    Value catchVal
);
int iseqAddEnsureRow(
    Iseq *seq,
    int ifrom,
    int ito,
    int itarget
);

// debugging
void debugInsn(Insn *insn);

#ifdef __cplusplus
}
#endif

#endif
