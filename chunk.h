#ifndef clox_chunk_h
#define clox_chunk_h

#include "value.h"

typedef struct {
    int count;
    int capacity;
    uint8_t *code;
    int *lines;
    ValueArray constants;
} Chunk;

void initChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte, int lineno);
void freeChunk(Chunk *chunk);
int addConstant(Chunk *chunk, Value value);
Value getConstant(Chunk *chunk, int idx);

#endif
