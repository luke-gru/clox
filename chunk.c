#include "common.h"
#include "chunk.h"
#include "memory.h"
#include "debug.h"

void initChunk(Chunk *chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL; // parallel arrays
    chunk->lines = NULL; // parallel arrays
    chunk->catchTbl = NULL;
    initValueArray(&chunk->constants);
}

/**
 * Write 1 byte of bytecode operation/data to chunk. Chunk
 * grows automatically if no more space.
 */
void writeChunk(Chunk *chunk, uint8_t byte, int line) {
    int prevCapa = chunk->capacity;
    if (chunk->count == prevCapa) {
        int capa = prevCapa;
        capa = GROW_CAPACITY(capa);
        chunk->code = GROW_ARRAY(chunk->code, uint8_t, prevCapa, capa);
        chunk->lines = GROW_ARRAY(chunk->lines, int, prevCapa, capa);
        chunk->capacity = capa;
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

static void freeCatchTable(CatchTable *catchTbl) {
    ASSERT(catchTbl);
    CatchTable *row = catchTbl;
    CatchTable *nextRow = NULL;
    while (row) {
        nextRow = row->next;
        free(row);
        row = nextRow;
    }
}

/**
 * Free all internal chunk structures, and reset it to a usable state.
 * Does NOT free the provided pointer.
 */
void freeChunk(Chunk *chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    freeValueArray(&chunk->constants);
    if (chunk->catchTbl) freeCatchTable(chunk->catchTbl);
    initChunk(chunk);
}

/**
 * Add a constant to constant pool and return its index into the pool
 */
int addConstant(Chunk* chunk, Value value) {
    writeValueArray(&chunk->constants, value);
    return chunk->constants.count - 1;
}

/**
 * Retrieve a constant from the provided chunk's constant pool
 */
Value getConstant(Chunk *chunk, int idx) {
    return chunk->constants.values[idx];
}

// returns index to newly added catch table
int addCatchRow(
    Chunk *chunk,
    int ifrom,
    int ito,
    int itarget,
    Value catchVal
) {
    CatchTable *tblRow = ALLOCATE(CatchTable, 1);
    tblRow->ifrom = ifrom;
    tblRow->ito = ito;
    tblRow->itarget = itarget;
    tblRow->catchVal = catchVal;
    memset(&tblRow->lastThrownValue, 0, sizeof(Value));
    tblRow->next = NULL;

    CatchTable *row = chunk->catchTbl;
    int idx = 0;
    if (row == NULL) {
        chunk->catchTbl = tblRow;
        return idx;
    }
    while (row->next) {
        row = row->next;
        idx++;
    }
    row->next = tblRow;
    return idx;
}
