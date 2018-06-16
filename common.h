#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// NOTE: when adding/removing from here, add/remove from opName() function in debug.c!
typedef enum {
    OP_CONSTANT,

    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NEGATE,
    OP_NOT,

    OP_GET_LOCAL, // get local var, next byte is frame slot index
    OP_SET_LOCAL, // set local var, next byte is frame slot index, value is stacktop
    OP_GET_GLOBAL, // get global var, next byte is frame slot index
    OP_SET_GLOBAL, // set global var, next byte is frame slot index, value is stacktop
    OP_DEFINE_GLOBAL, // define global var for first time
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,

    OP_PROP_GET,
    OP_PROP_SET,

    OP_CALL,
    OP_RETURN,
    OP_PRINT,

    OP_TRUE,
    OP_FALSE,
    OP_NIL,

    OP_AND,
    OP_OR,

    OP_POP,

    OP_EQUAL,
    OP_GREATER,
    OP_LESS,

    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,

    OP_CLASS, // top of stack is variable representing class
    OP_SUBCLASS, // top of stack is variable representing subclass

    OP_LEAVE,
} OpCode;

#endif
