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

void initParser(Parser *p);
Node *parse(Parser *p);

extern Parser parser; // main parser

#endif
