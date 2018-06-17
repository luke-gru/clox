#ifndef clox_chunk_h
#define clox_chunk_h

#include "value.h"

typedef struct CatchTable CatchTable;
typedef struct CatchTable {
    int ifrom; // instruction try start
    int ito; // instruction try end
    int itarget; // instruction try catch start
    Value catchVal; // catch class or other value
    Value lastThrownValue; // runtime VM value of last thrown instance
    CatchTable *next;
} CatchTable;

typedef struct {
    int count;
    int capacity;
    uint8_t *code;
    int *lines;
    ValueArray constants;
    CatchTable *catchTbl;
} Chunk;

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
