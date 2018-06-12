#ifndef clox_parser_h
#define clox_parser_h

#include "scanner.h"
#include "object.h"
#include "vec.h"
#include "nodes.h"

typedef vec_t(Token) vec_tok_t;

typedef struct sParser {
  bool hadError;
  bool panicMode;
  Token current;
  Token previous;
  vec_tok_t peekBuf;
} Parser;

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

typedef enum {
  TYPE_FUNCTION,
  TYPE_INITIALIZER,
  TYPE_METHOD,
  TYPE_TOP_LEVEL
} FunctionType;

void initParser(void);
Node *parse(void);

extern Parser parser;

#endif
