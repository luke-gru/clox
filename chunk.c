#include "common.h"
#include "chunk.h"
#include "memory.h"
#include "debug.h"

void initChunk(Chunk *chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
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
    chunk->code = NULL;
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    chunk->lines = NULL;
    freeValueArray(&chunk->constants);
    if (chunk->catchTbl) {
        freeCatchTable(chunk->catchTbl);
        chunk->catchTbl = NULL;
    }
    initChunk(chunk);
}

/**
 * Add a constant to constant pool and return its index into the pool
 */
int addConstant(Chunk *chunk, Value value) {
    if (IS_OBJ(value)) {
        hideFromGC(AS_OBJ(value));
    }
    writeValueArray(&chunk->constants, value);
    if (IS_OBJ(value)) {
        unhideFromGC(AS_OBJ(value));
    }
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
        return 0;
    }
    idx++;
    while (row->next) {
        row = row->next;
        idx++;
    }
    row->next = tblRow;
    return idx;
}
