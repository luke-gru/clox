#ifndef clox_parser_h
#define clox_parser_h

#ifdef __cplusplus
extern "C" {
#endif

#include <setjmp.h>
#include "scanner.h"
#include "object.h"
#include "vec.h"
#include "nodes.h"

typedef vec_t(Token) vec_tok_t;

typedef struct sParser {
  Token current;
  Token previous;
  vec_tok_t peekBuf;
  bool inCallExpr;

  vec_void_t v_errMessages; // list of ObjStrings
  bool hadError;
  bool panicMode;
  jmp_buf errJmpBuf;
} Parser;

void initParser(Parser *p);
void freeParser(Parser *p); // frees parser, but NOT the nodes
Node *parse(Parser *p);
Node *parseExpression(Parser *p);
void outputParserErrors(Parser *p, FILE *f);

#ifdef __cplusplus
}
#endif

#endif
