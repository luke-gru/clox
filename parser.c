#include "parser.h"
#include "options.h"

Parser parser;

void initParser(Parser *p) {
    if (CLOX_OPTION_T(debugParser)) {
        char *parserOutputInit = "Parser output:\n";
        parser->debugBuf = copyString(parserOutputInit, strlen(parserOutputInit));
    }
}

static void errorAt(Token *token, const char *message) {
  if (parser.panicMode) return;
  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing.
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}


static void error(const char *message) {
  errorAt(&parser.previous, message);
}
static void errorAtCurrent(const char *message) {
  errorAt(&parser.current, message);
}

// Fill `parser.current` with next token in stream
static void advance() {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;

    errorAtCurrent(parser.current.start);
  }
}

// Expect current token to be of given type, otherwise error with given
// message.
static void consume(TokenType type, const char *message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}


// Check that the current token is of given type
static bool check(TokenType type) {
  return parser.current.type == type;
}

// If current token is of given type, advance to next.
// Otherwise returns false.
static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

// forward decls
static void expression();
static void statement();
static void declaration();
static ParseRule *getRule(TokenType type);
static void parsePrecedence(Precedence precedence);


static uint8_t parseVariable(const char *errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);
  if (CLOX_OPTION_T(debugParser)) {
  }
/* Global Variables not-yet < Local Variables not-yet
  return identifierConstant(&parser.previous);
*/
//> Local Variables not-yet

  // If it's a global variable, create a string constant for it.
  if (current->scopeDepth == 0) {
    return identifierConstant(&parser.previous);
  }

  declareVariable();
  return 0;
//< Local Variables not-yet
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}
