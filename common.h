#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    OP_CONSTANT,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NEGATE,
    OP_NOT,
    OP_RETURN,
    OP_NIL,
    OP_GET_LOCAL, // get local var, next byte is frame slot index
    OP_SET_LOCAL, // set local var, next byte is frame slot index, value is stacktop
    OP_CALL,
    OP_PRINT,
    OP_TRUE,
    OP_FALSE,
} OpCode;

#endif
