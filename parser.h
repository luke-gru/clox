#ifndef clox_parser_h
#define clox_parser_h

#include "scanner.h"
#include "object.h"

typedef struct sParser {
  bool hadError;
  bool panicMode;
  Token current;
  Token previous;
  ObjString *debugBuf;
} Parser;

// precedence, low to high
typedef enum {
  PREC_NONE, // lowest
  PREC_ASSIGNMENT,  // =
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! - +
  PREC_CALL,        // . () []
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct sParseRule {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct sLocal {
  // The name of the local variable.
  Token name;
  // The depth in the scope chain that this variable was declared at. Zero is
  // the outermost scope--parameters for a method, or the first local block in
  // top level code. One is the scope within that, etc.
  int depth;
  // True if this local variable is captured as an upvalue by a function.
  bool isUpvalue;
} Local;

typedef struct {
  // The index of the local variable or upvalue being captured from the
  // enclosing function.
  uint8_t index;

  // Whether the captured variable is a local or upvalue in the enclosing
  // function.
  bool isLocal;
} Upvalue;
//< Closures not-yet
//> Calls and Functions not-yet

typedef enum {
  TYPE_FUNCTION,
  TYPE_INITIALIZER,
  TYPE_METHOD,
  TYPE_TOP_LEVEL
} FunctionType;

void initParser(Parser *p);

#endif
