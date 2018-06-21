#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// NOTE: when adding/removing from here, add/remove from opName() function in debug.c!
typedef enum {
    OP_CONSTANT = 1,

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
    OP_METHOD, // define a method in the VM, string constant index as operand, function object at top of stack, class object under that

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
    OP_JUMP_IF_FALSE, // pops value off top of stack, checks truthiness
    OP_JUMP_IF_FALSE_PEEK, // peeks value off top of stack, checks truthiness
    OP_JUMP_IF_TRUE_PEEK, // peeks value off top of stack, checks truthiness
    OP_LOOP,

    OP_CREATE_ARRAY, // number of elements is at top of stack, elements are below it

    OP_CLASS, // class name is given as operand
    OP_SUBCLASS, // top of stack is superclass, operand is class name

    OP_THROW,
    OP_GET_THROWN,

    OP_INDEX_GET,
    OP_INDEX_SET,

    OP_LEAVE,
} OpCode;

#endif
