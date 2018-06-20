#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "scanner.h"
#include "stdlib.h"
#include "memory.h"
#include "options.h"

// global
Scanner scanner;

typedef struct {
  const char *name;
  size_t      length;
  TokenType   type;
} Keyword;

// The table of reserved words and their associated token types.
static Keyword keywords[] = {
  {"and",     3, TOKEN_AND},
  {"class",   5, TOKEN_CLASS},
  {"module",  6, TOKEN_MODULE},
  {"else",    4, TOKEN_ELSE},
  {"false",   5, TOKEN_FALSE},
  {"for",     3, TOKEN_FOR},
  {"fun",     3, TOKEN_FUN},
  {"if",      2, TOKEN_IF},
  {"nil",     3, TOKEN_NIL},
  {"try",     3, TOKEN_TRY},
  {"catch",   5, TOKEN_CATCH},
  {"throw",   5, TOKEN_THROW},
  {"or",      2, TOKEN_OR},
  {"print",   5, TOKEN_PRINT},
  {"return",  6, TOKEN_RETURN},
  {"super" ,  5, TOKEN_SUPER},
  {"this",    4, TOKEN_THIS},
  {"true",    4, TOKEN_TRUE},
  {"var",     3, TOKEN_VAR},
  {"while",   5, TOKEN_WHILE},
  {"continue",   8, TOKEN_CONTINUE},
  {"break",   5, TOKEN_BREAK},
  // Sentinel to mark the end of the array.
  {NULL,      0, TOKEN_EOF}
};

// Returns true if `c` is an English letter or underscore.
static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') ||
    (c >= 'A' && c <= 'Z') ||
    c == '_';
}

// Returns true if `c` is a digit.
static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

// Returns true if `c` is an English letter, underscore, or digit.
static bool isAlphaNumeric(char c) {
  return isAlpha(c) || isDigit(c);
}

static bool isAtEnd() {
  return *scanner.current == '\0';
}

static char advance() {
  scanner.current++;
  return scanner.current[-1];
}

static char peek() {
  return *scanner.current;
}

static char peekNext() {
  if (isAtEnd()) return '\0';
  return scanner.current[1];
}

static bool match(char expected) {
  if (isAtEnd()) return false;
  if (*scanner.current != expected) return false;

  scanner.current++;
  return true;
}

static Token makeToken(TokenType type) {
  Token token;
  token.type = type;
  token.start = scanner.tokenStart;
  token.length = (int)(scanner.current - scanner.tokenStart);
  token.lexeme = NULL; // only created on demand, see tokStr()
  token.line = scanner.line;
  if (CLOX_OPTION_T(debugTokens)) {
      fprintf(stderr, "Tok: %s\n", tokTypeStr(type));
  }
  return token;
}

static Token errorToken(const char *message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = scanner.line;
  return token;
}

static void skipWhitespace() {
  for (;;) {
    char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;

      case '\n':
        scanner.line++;
        advance();
        break;

      case '/':
        if (peekNext() == '/') {
          // A comment goes until the end of the line.
          while (peek() != '\n' && !isAtEnd()) advance();
        } else {
          return;
        }
        break;

      default:
        return;
    }
  }
}

static Token identifier() {
  while (isAlphaNumeric(peek())) advance();

  TokenType type = TOKEN_IDENTIFIER;

  // See if the identifier is a reserved word.
  size_t length = scanner.current - scanner.tokenStart;
  for (Keyword *keyword = keywords; keyword->name != NULL; keyword++) {
    if (length == keyword->length &&
        memcmp(scanner.tokenStart, keyword->name, length) == 0) {
      type = keyword->type;
      break;
    }
  }

  return makeToken(type);
}

static Token number() {
  while (isDigit(peek())) advance();

  // Look for a fractional part.
  if (peek() == '.' && isDigit(peekNext())) {
    // Consume the "."
    advance();

    while (isDigit(peek())) advance();
  }

  return makeToken(TOKEN_NUMBER);
}

static Token string() {
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\n') scanner.line++;
    advance();
  }

  // Unterminated string.
  if (isAtEnd()) return errorToken("Unterminated string.");

  // The closing ".
  advance();
  return makeToken(TOKEN_STRING);
}

Token scanToken() {
  skipWhitespace();

  scanner.tokenStart = scanner.current;
  if (isAtEnd()) return makeToken(TOKEN_EOF);

  char c = advance();

  if (isAlpha(c)) return identifier();
  if (isDigit(c)) return number();

  switch (c) {
    case '(': return makeToken(TOKEN_LEFT_PAREN);
    case ')': return makeToken(TOKEN_RIGHT_PAREN);
    case '{': {
        scanner.indent++;
        return makeToken(TOKEN_LEFT_BRACE);
    }
    case '}': {
        scanner.indent--;
        return makeToken(TOKEN_RIGHT_BRACE);
    }
    case '[': return makeToken(TOKEN_LEFT_BRACKET);
    case ']': return makeToken(TOKEN_RIGHT_BRACKET);
    case ';': return makeToken(TOKEN_SEMICOLON);
    case ',': return makeToken(TOKEN_COMMA);
    case '.': return makeToken(TOKEN_DOT);
    case '-': return makeToken(TOKEN_MINUS);
    case '+': return makeToken(TOKEN_PLUS);
    case '/': return makeToken(TOKEN_SLASH);
    case '*': return makeToken(TOKEN_STAR);
    case '!':
      if (match('=')) return makeToken(TOKEN_BANG_EQUAL);
      return makeToken(TOKEN_BANG);

    case '=':
      if (match('=')) return makeToken(TOKEN_EQUAL_EQUAL);
      return makeToken(TOKEN_EQUAL);

    case '<':
      if (match('=')) return makeToken(TOKEN_LESS_EQUAL);
      return makeToken(TOKEN_LESS);

    case '>':
      if (match('=')) return makeToken(TOKEN_GREATER_EQUAL);
      return makeToken(TOKEN_GREATER);

    case '"': return string();
  }

  return errorToken("Unexpected character.");
}

void initScanner(const char *src) {
  scanner.source = src;
  scanner.tokenStart = src;
  scanner.current = src;
  scanner.line = 1;
  scanner.indent = 0;
}

void resetScanner(void) {
  scanner.tokenStart = scanner.source;
  scanner.current = scanner.source;
  scanner.line = 1;
  scanner.indent = 0;
}

const char *tokTypeStr(TokenType ttype) {
  switch (ttype) {
    case TOKEN_EMPTY:
      return "(EMPTY)";
    case TOKEN_LEFT_PAREN:
      return "LEFT_PAREN";
    case TOKEN_RIGHT_PAREN:
      return "RIGHT_PAREN";
    case TOKEN_LEFT_BRACE:
      return "LEFT_BRACE";
    case TOKEN_RIGHT_BRACE:
      return "RIGHT_BRACE";
    case TOKEN_LEFT_BRACKET:
      return "LEFT_BRACKET";
    case TOKEN_RIGHT_BRACKET:
      return "RIGHT_BRACKET";
    case TOKEN_BANG:
      return "BANG";
    case TOKEN_BANG_EQUAL:
      return "BANG_EQUAL";
    case TOKEN_COMMA:
      return "COMMA";
    case TOKEN_DOT:
      return "DOT";
    case TOKEN_EQUAL:
      return "EQUAL";
    case TOKEN_EQUAL_EQUAL:
      return "EQUAL_EQUAL";
    case TOKEN_GREATER:
      return "GREATER";
    case TOKEN_GREATER_EQUAL:
      return "GREATER_EQUAL";
    case TOKEN_LESS:
      return "LESS";
    case TOKEN_LESS_EQUAL:
      return "LESS_EQUAL";
    case TOKEN_MINUS:
      return "MINUS";
    case TOKEN_PLUS:
      return "PLUS";
    case TOKEN_SEMICOLON:
      return "SEMICOLON";
    case TOKEN_SLASH:
      return "SLASH";
    case TOKEN_STAR:
      return "STAR";
    case TOKEN_IDENTIFIER:
      return "IDENTIFIER";
    case TOKEN_STRING:
      return "STRING";
    case TOKEN_NUMBER:
      return "NUMBER";
    case TOKEN_AND:
      return "AND";
    case TOKEN_CLASS:
      return "CLASS";
    case TOKEN_MODULE:
      return "MODULE";
    case TOKEN_ELSE:
      return "ELSE";
    case TOKEN_FALSE:
      return "FALSE";
    case TOKEN_FUN:
      return "FUN";
    case TOKEN_FOR:
      return "FOR";
    case TOKEN_IF:
      return "IF";
    case TOKEN_NIL:
      return "NIL";
    case TOKEN_OR:
      return "OR";
    case TOKEN_PRINT:
      return "PRINT";
    case TOKEN_RETURN:
      return "RETURN";
    case TOKEN_SUPER:
      return "SUPER";
    case TOKEN_THIS:
      return "THIS";
    case TOKEN_TRUE:
      return "TRUE";
    case TOKEN_VAR:
      return "VAR";
    case TOKEN_WHILE:
      return "WHILE";
    case TOKEN_ERROR:
      return "!!ERROR!!";
    case TOKEN_EOF:
      return "EOF";
    default:
      return "!!UNKNOWN!!";
  }
}

void scanAllPrint(const char *src) {
    initScanner(src);
    int line = -1;
    for (;;) {
        Token token = scanToken();
        if (token.line != line) {
            printf("%4d ", token.line);
            line = token.line;
        } else {
            printf("   | ");
        }
        printf("%10s '%.*s'\n", tokTypeStr(token.type), token.length, token.start);

        if (token.type == TOKEN_EOF) break;
    }
}

char *tokStr(Token *tok) {
    if (tok->lexeme != NULL) return tok->lexeme;
    char *buf = calloc(tok->length+1, 1);
    memcpy(buf, tok->start, tok->length);
    tok->lexeme = buf;
    return buf;
}

Token emptyTok(void) {
    Token t = {
        .type = TOKEN_EMPTY,
        .start = NULL,
        .lexeme = NULL,
        .length = 0,
        .line = 0,
    };
    return t;
}

// copy given token to the heap
Token *copyToken(Token *tok) {
    Token *ret = ALLOCATE(Token, 1);
    memcpy(ret, tok, sizeof(Token));
    return ret;
}
