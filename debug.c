#include <stdio.h>

#include "debug.h"

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

/**
 * Print all operations and operands to the console.
 */
void disassembleChunk(Chunk *chunk, const char *name) {
  printf("== %s ==\n", name);

  for (int i = 0; i < chunk->count;) {
    i = disassembleInstruction(chunk, i);
    if (i <= 0) {
        break;
    }
  }
}

static int constantInstruction(char *op, Chunk *chunk, int i) {
    uint8_t constantIdx = chunk->code[i + 1];
    printf("%-16s %4d '", op, constantIdx);
    printValue(getConstant(chunk, constantIdx));
    printf("'\n");
    return i+2;
}

static int simpleInstruction(char *op, int i) {
    printf("%s\n", op);
    return i+1;
}

int disassembleInstruction(Chunk *chunk, int i) {
    printf("%04d ", i);
    // same line as prev instruction
    if (i > 0 && chunk->lines[i] == chunk->lines[i - 1]) {
        printf("   | ");
    } else { // new line
        printf("%4d ", chunk->lines[i]);
    }
    uint8_t byte = chunk->code[i];
    switch (byte) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, i);
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE", i);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", i);
        case OP_ADD:
            return simpleInstruction("OP_ADD", i);
        case OP_SUBTRACT:
            return simpleInstruction("OP_SUBTRACT", i);
        case OP_MULTIPLY:
            return simpleInstruction("OP_MULTIPLY", i);
        case OP_DIVIDE:
            return simpleInstruction("OP_DIVIDE", i);
        default:
            printf("Unknown opcode %d\n", byte);
            return -1;
    }
}
