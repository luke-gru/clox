#include "common.h"
#include "chunk.h"
#include "memory.h"
#include "object.h"
#include "errno.h"
#include "debug.h"

void initChunk(Chunk *chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL; // parallel arrays
    chunk->lines = NULL; // parallel arrays
    initValueArray(&chunk->constants);
}

/**
 * Write 1 byte of bytecode operation/data to chunk. Chunk
 * grows automatically if no more space.
 */
void writeChunk(Chunk *chunk, uint8_t byte, int line) {
    int prevCapa = chunk->capacity;
    if (chunk->count <= prevCapa) {
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

/**
 * Free all internal chunk structures, and reset it to a usable state.
 * Does NOT free the provided pointer.
 */
void freeChunk(Chunk *chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    freeValueArray(&chunk->constants);
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

int serializeChunk(Chunk *chunk, FILE *file, int *errcode) {
    ASSERT(file); ASSERT(chunk);
    size_t written = fwrite(&chunk->count, sizeof(int), 1, file);
    if (written == 0) {
        fprintf(stderr, "serializeChunk error 1\n");
        *errcode = errno;
        return -1;
    }
    written = fwrite(chunk->code, chunk->count * sizeof(uint8_t), 1, file);
    if (written == 0) {
        fprintf(stderr, "serializeChunk error 2\n");
        *errcode = errno;
        return -1;
    }
    written = fwrite(chunk->lines, chunk->count * sizeof(int), 1, file);
    if (written == 0) {
        fprintf(stderr, "serializeChunk error 3\n");
        *errcode = errno;
        return -1;
    }
    ValueArray constants = chunk->constants;
    fprintf(stderr, "Serializing chunk constants - num: %d\n", constants.count);
    for (int i = 0; i < constants.count; i++) {
        written = serializeValue(constants.values+i, file, errcode);
        if (written == -1) {
            fprintf(stderr, "serializeChunk error 4\n");
            return -1;
        }
    }
    return 0;
}

int loadChunk(Chunk *chunk, FILE *file, int *errcode) {
    ASSERT(file); ASSERT(chunk);
    size_t read = fread(&chunk->count, sizeof(int), 1, file);
    ASSERT(chunk->count > 0);
    if (chunk->code == NULL) {
        chunk->code = ALLOCATE(uint8_t, chunk->count+1);
    }
    if (chunk->capacity < chunk->count) {
        chunk->capacity = chunk->count;
    }
    if (read == 0) {
        fprintf(stderr, "Failed to load count\n");
        *errcode = errno;
        return -1;
    }
    read = fread(chunk->code, chunk->count, 1, file);
    if (read == 0) {
        fprintf(stderr, "Failed to load code\n");
        *errcode = errno;
        return -1;
    }
    if (chunk->lines == NULL) {
        chunk->lines = ALLOCATE(int, chunk->count+1);
    }
    read = fread(chunk->lines, chunk->count * sizeof(int), 1, file);
    if (read == 0) {
        fprintf(stderr, "Failed to load lines\n");
        *errcode = errno;
        return -1;
    }
    int bytesToRead = 0;
    ValueArray *constants = &chunk->constants;
    while (fread(&bytesToRead, sizeof(int), 1, file) > 0) {
        if (bytesToRead == 0) return -1;
        char typeToRead = '?';
        fread(&typeToRead, 1, 1, file);
        ASSERT(typeToRead != '?');
        switch (typeToRead) {
        case 'n': { // nil
            fprintf(stderr, "Loading constant nil\n");
            writeValueArray(constants, NIL_VAL);
            break;
        }
        case 't': { // true
            fprintf(stderr, "Loading constant true\n");
            writeValueArray(constants, BOOL_VAL(true));
            break;
        }
        case 'f': { // false
            fprintf(stderr, "Loading constant false\n");
            writeValueArray(constants, BOOL_VAL(false));
            break;
        }
        case 'd': { // double (number)
            fprintf(stderr, "Loading constant number\n");
            double d = 0.0;
            fread(&d, sizeof(double), 1, file);
            writeValueArray(constants, NUMBER_VAL(d));
            break;
        }
        case 's': { // string
            ASSERT(bytesToRead > 0); // includes '\0' char
            char *str = calloc(bytesToRead+1, 1);
            ASSERT_MEM(str);
            fread(str, bytesToRead, 1, file);
            fprintf(stderr, "Loading constant string: %s\n", str);
            writeValueArray(constants,
                OBJ_VAL(copyString(str, strlen(str)))
            );
            break;
        }
        case 'c': { // function
            ASSERT(bytesToRead > 0);
            char *nameBuf = calloc(bytesToRead, 1);
            ASSERT_MEM(nameBuf);
            int arity = 0;
            fread(&arity, sizeof(int), 1, file);
            fread(nameBuf, bytesToRead, 1, file);
            fprintf(stderr, "Loading constant string: %s\n", nameBuf);
            fprintf(stderr, "function name read: %s\n", nameBuf);
            ObjString *name = copyString(nameBuf, strlen(nameBuf));
            Chunk funcChunk;
            initChunk(&funcChunk);
            int loadFuncRes = loadChunk(&funcChunk, file, errcode);
            fprintf(stderr, "loadResFunc: %d\n", loadFuncRes);
            ASSERT(loadFuncRes == 0);
            ObjFunction *func = newFunction(&funcChunk); // copies the Chunk struct
            func->name = name;
            func->arity = arity;
            writeValueArray(constants, OBJ_VAL(func));
            break;
        default:
            fprintf(stderr, "default in loadChunk, type to read: %c\n", typeToRead);
            return -1;
        }
        }
    }
    return 0;
}
