#include <stdio.h>

#include "debug.h"
#include "object.h"

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

char *opName(OpCode code) {
    switch (code) {
    case OP_CONSTANT:
        return "OP_CONSTANT";
    case OP_ADD:
        return "OP_ADD";
    case OP_SUBTRACT:
        return "OP_SUBTRACT";
    case OP_MULTIPLY:
        return "OP_MULTIPLY";
    case OP_DIVIDE:
        return "OP_DIVIDE";
    case OP_NEGATE:
        return "OP_NEGATE";
    case OP_NOT:
        return "OP_NOT";
    case OP_RETURN:
        return "OP_RETURN";
    case OP_NIL:
        return "OP_NIL";
    default:
        return "!Unknown instruction!";
    }
}

/**
 * Print all operations and operands to the console.
 */
void printDisassembledChunk(Chunk *chunk, const char *name) {
  printf("== %s ==\n", name);

  for (int i = 0; i < chunk->count;) {
    i = printDisassembledInstruction(chunk, i);
    if (i <= 0) {
        break;
    }
  }
}

static int printConstantInstruction(char *op, Chunk *chunk, int i) {
    uint8_t constantIdx = chunk->code[i + 1];
    printf("%-16s %4d '", op, constantIdx);
    printValue(getConstant(chunk, constantIdx));
    printf("'\n");
    return i+2;
}

static int constantInstruction(ObjString *buf, char *op, Chunk *chunk, int i) {
    uint8_t constantIdx = chunk->code[i + 1];

    char *cbuf = calloc(strlen(op)+1+6, 1);
    ASSERT_MEM(cbuf);
    sprintf(cbuf, "%s\t%04d\n", op, constantIdx);
    pushCString(buf, cbuf, strlen(cbuf));
    return i+2;
}

static int printSimpleInstruction(char *op, int i) {
    printf("%s\n", op);
    return i+1;
}

static int simpleInstruction(ObjString *buf, char *op, int i) {
    pushCString(buf, op, strlen(op));
    pushCString(buf, "\n", 1);
    return i+1;
}

int printDisassembledInstruction(Chunk *chunk, int i) {
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
            return printConstantInstruction(opName(byte), chunk, i);
        case OP_NEGATE:
        case OP_RETURN:
        case OP_ADD:
        case OP_SUBTRACT:
        case OP_MULTIPLY:
        case OP_DIVIDE:
            return printSimpleInstruction(opName(byte), i);
        default:
            printf("Unknown opcode %d\n", byte);
            return -1;
    }
}

static int disassembledInstruction(ObjString *buf, Chunk *chunk, int i) {
    char *numBuf = calloc(5+1, 1);
    ASSERT_MEM(numBuf);
    sprintf(numBuf, "%04d\t", i);
    pushCString(buf, numBuf, strlen(numBuf));
    uint8_t byte = chunk->code[i];
    switch (byte) {
        case OP_CONSTANT:
            return constantInstruction(buf, opName(byte), chunk, i);
        case OP_NEGATE:
        case OP_RETURN:
        case OP_ADD:
        case OP_SUBTRACT:
        case OP_MULTIPLY:
        case OP_DIVIDE:
            return simpleInstruction(buf, opName(byte), i);
        default: {
            char *cBuf = calloc(19+1, 1);
            ASSERT_MEM(cBuf);
            sprintf(cBuf, "Unknown opcode %03d\n", byte);
            pushCString(buf, cBuf, strlen(cBuf));
            return -1;
        }
    }
}

ObjString *disassembleChunk(Chunk *chunk) {
    ObjString *buf = copyString("", 0);
    for (int i = 0; i < chunk->count;) {
        i = disassembledInstruction(buf, chunk, i);
        if (i <= 0) {
            break;
        }
    }
    return buf;
}
