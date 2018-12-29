#include "common.h"
#include "chunk.h"
#include "memory.h"
#include "debug.h"

void initChunk(Chunk *chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->ndepths = NULL;
    chunk->nwidths = NULL;
    chunk->catchTbl = NULL;
    chunk->constants = ALLOCATE(ValueArray, 1);
    initValueArray(chunk->constants);
}

void initIseq(Iseq *seq) {
    seq->count = 0;
    seq->byteCount = 0;
    seq->constants = NULL; // share constants with chunk
    seq->catchTbl = NULL; // share catchTbl with chunk
    seq->tail = NULL;
    seq->insns = NULL;
}

// just zero it out, as well as the instructions
void freeIseq(Iseq *seq) {
    Insn *in = seq->insns;
    int idx = 0;
    while (in) {
        Insn *next = in->next;
        // TODO: there's a memory corruption error here if I try
        // to free the memory for some reason. Need to investigate.
        /*xfree(in);*/
        memset(in, 0, sizeof(*in));
        in = next;
        idx++;
    }
    seq->insns = NULL;
    seq->count = 0;
    seq->byteCount = 0;
    seq->tail = NULL;
    // catchtbl is shared with chunk, don't free it
    seq->catchTbl = NULL;
    // constant valuearray is shared with chunk, don't free it
    seq->constants = NULL;
}

void iseqAddInsn(Iseq *seq, Insn *toAdd) {
    Insn *prev = seq->tail;
    if (prev) {
        prev->next = toAdd;
    } else {
        seq->insns = toAdd;
    }
    toAdd->prev = prev;
    toAdd->next = NULL;
    seq->tail = toAdd;
    seq->count++;
    seq->byteCount += (toAdd->numOperands+1);
}

// removes and frees the given insn
bool iseqRmInsn(Iseq *seq, Insn *toRm) {
    Insn *in = seq->insns;
    if (in == NULL) return false;
    while (in && in != toRm) {
        in = in->next;
    }
    if (!in) return false;
    if (in->prev) {
        in->prev->next = in->next;
    } else {
        seq->insns = in->next;
    }
    if (in->next) {
        in->next->prev = in->prev;
    } else {
        seq->tail = in->prev;
    }
    if (seq->tail == NULL) {
        seq->insns = NULL;
    }
    seq->count--;
    seq->byteCount -= (toRm->numOperands+1);
    xfree(toRm);
    return true;
}

size_t iseqInsnByteDiff(Insn *prev, Insn *after) {
    ASSERT(after);
    if (prev == after) return 0;
    size_t diff = 0;
    Insn *cur = after;
    while (cur && cur != prev) {
        diff += cur->numOperands+1;
        cur = cur->prev;
    }
    if (cur) ASSERT(cur == prev);
    return diff;
}

/**
 * Write 1 byte of bytecode operation/data to chunk. Chunk
 * grows automatically if no more space.
 */
void writeChunk(Chunk *chunk, uint8_t byte, int line, int nDepth, int nWidth) {
    int prevCapa = chunk->capacity;
    if (chunk->count == prevCapa) {
        int capa = prevCapa;
        capa = GROW_CAPACITY(capa);
        chunk->code = GROW_ARRAY(chunk->code, uint8_t, prevCapa, capa);
        chunk->lines = GROW_ARRAY(chunk->lines, int, prevCapa, capa);
        chunk->ndepths = GROW_ARRAY(chunk->ndepths, int, prevCapa, capa);
        chunk->nwidths = GROW_ARRAY(chunk->nwidths, int, prevCapa, capa);
        chunk->capacity = capa;
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->ndepths[chunk->count] = nDepth;
    chunk->nwidths[chunk->count] = nWidth;
    chunk->count++;
}

static void freeCatchTable(CatchTable *catchTbl) {
    ASSERT(catchTbl);
    CatchTable *row = catchTbl;
    CatchTable *nextRow = NULL;
    while (row) {
        nextRow = row->next;
        xfree(row);
        row = nextRow;
    }
}

/**
 * Free all internal chunk structures, and reset it to a usable state.
 * Does NOT free the provided pointer.
 */
void freeChunk(Chunk *chunk) {
    if (chunk->code) {
        /*fprintf(stderr, "freeChunk code\n");*/
        FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    }
    chunk->code = NULL;
    if (chunk->lines) {
        /*fprintf(stderr, "freeChunk lines\n");*/
        FREE_ARRAY(int, chunk->lines, chunk->capacity);
    }
    chunk->lines = NULL;
    /*fprintf(stderr, "freeChunk constants\n");*/
    freeValueArray(chunk->constants);
    if (chunk->catchTbl) {
        /*fprintf(stderr, "freeChunk catchTbl\n");*/
        freeCatchTable(chunk->catchTbl);
        chunk->catchTbl = NULL;
    }
    /*fprintf(stderr, "freeChunk reinit\n");*/
    initChunk(chunk);
}

/**
 * Retrieve a constant from the provided chunk's constant pool
 */
Value getConstant(Chunk *chunk, int idx) {
    return chunk->constants->values[idx];
}

int iseqAddCatchRow(
    Iseq *seq,
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

    CatchTable *row = seq->catchTbl;
    int idx = 0;
    if (row == NULL) {
        seq->catchTbl = tblRow;
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

int iseqAddConstant(Iseq *seq, Value value) {
    writeValueArrayEnd(seq->constants, value);
    return seq->constants->count - 1;
}
