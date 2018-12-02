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
    OP_SET_LOCAL, // set local var, next byte is frame slot index, value is on top of stack
    OP_GET_GLOBAL, // get global var, next byte is frame slot index
    OP_SET_GLOBAL, // set global var, next byte is frame slot index, value is on top of stack
    OP_DEFINE_GLOBAL, // define global var for first time

    OP_CLOSURE,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_CLOSE_UPVALUE,

    OP_PROP_GET,
    OP_PROP_SET,
    OP_METHOD, // define a method in the VM, string constant index as operand, function object at top of stack, class object under that
    OP_CLASS_METHOD, // define a class method in the VM, string constant index as operand, function object at top of stack, class object under that
    OP_GETTER, // define a getter method in the VM, string constant index as operand, function object at top of stack, class object under that
    OP_SETTER, // define a setter method in the VM, string constant index as operand, function object at top of stack, class object under that

    OP_CALL, // call function, arguments are on stack
    OP_INVOKE, // call regular method, instance and arguments are on stack
    OP_SPLAT_ARRAY,
    OP_GET_THIS,
    OP_GET_SUPER, // method lookup begins in superclass, class and instance are on stack
    OP_RETURN,
    OP_PRINT,

    OP_STRING,

    OP_TRUE,
    OP_FALSE,
    OP_NIL,

    OP_AND,
    OP_OR,

    OP_POP,

    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_GREATER_EQUAL,
    OP_LESS_EQUAL,

    OP_JUMP,
    OP_JUMP_IF_FALSE, // pops value off top of stack, checks truthiness
    OP_JUMP_IF_TRUE, // pops value off top of stack, checks truthiness
    OP_JUMP_IF_FALSE_PEEK, // peeks value off top of stack, checks truthiness
    OP_JUMP_IF_TRUE_PEEK, // peeks value off top of stack, checks truthiness
    OP_LOOP,

    OP_CLASS, // class name is given as operand
    OP_SUBCLASS, // top of stack is superclass, operand is class name
    OP_MODULE, // module name is given as operand
    OP_IN,

    OP_THROW,
    OP_GET_THROWN,

    OP_INDEX_GET,
    OP_INDEX_SET,

    OP_CHECK_KEYWORD,

    OP_LEAVE,
} OpCode;

#endif
