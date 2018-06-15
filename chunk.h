#ifndef clox_chunk_h
#define clox_chunk_h

#include <stdio.h>
#include "value.h"

typedef struct {
    int count; // bytes of code
    int capacity; // private field
    uint8_t *code; // automatically grows (see writeChunk)
    int *lines; // parallel indexes to bytecode
    ValueArray constants; // automatically grows (see addConstant)
} Chunk;

void initChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte, int lineno);
void freeChunk(Chunk *chunk);
int addConstant(Chunk *chunk, Value value);
Value getConstant(Chunk *chunk, int idx);
int serializeChunk(Chunk *chunk, FILE *file, int *errcode);
int loadChunk(Chunk *chunk, FILE *file, int *errcode);

#endif
