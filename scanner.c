#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "scanner.h"
#include "stdlib.h"
#include "memory.h"
#include "options.h"
#include "debug.h"

// global
Scanner scanner;
static Scanner *current = NULL;

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
  {"in",      2, TOKEN_IN},
  {"foreach", 7, TOKEN_FOREACH},
  {"__END__",   7, TOKEN_END_SCRIPT},
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
  return current->scriptEnded || *current->current == '\0';
}

static char advance() {
  current->current++;
  return current->current[-1];
}

static char peek() {
  return *current->current;
}

static char peekNext() {
  if (isAtEnd()) return '\0';
  return current->current[1];
}

static bool match(char expected) {
  if (isAtEnd()) return false;
  if (*current->current != expected) return false;

  current->current++;
  return true;
}

static void strReplace(char *str, char *substr, char replace) {
    char *found = NULL;
    int len = strlen(substr);
    int difflen = len-1;
    while ((found = strstr(str, substr)) != NULL) {
        *found = replace;
        if (len != 1) {
            int left = strlen(str)-(found-str);
            memmove(found+1, found+1+difflen, left-(difflen+1));
            memset(str + strlen(str)-difflen, 0, difflen);
        }
    }
}

static Token makeToken(TokenType type) {
  Token token;
  token.type = type;
  token.start = current->tokenStart;
  token.length = (int)(current->current - current->tokenStart);
  token.lexeme = NULL; // only created on demand, see tokStr()
  token.line = current->line;
  if (CLOX_OPTION_T(debugTokens)) {
      fprintf(stderr, "Tok: %s -> '%s'\n", tokTypeStr(type), tokStr(&token));
  }
  if (type == TOKEN_END_SCRIPT) {
      current->scriptEnded = true;
      return makeToken(TOKEN_EOF);
  }
  return token;
}

static Token errorToken(const char *message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = current->line;
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
        current->line++;
        advance();
        break;

      case '/':
        if (peekNext() == '/') {
          // A comment goes until the end of the line.
          while (peek() != '\n' && !isAtEnd()) advance();
        } else if (peekNext() == '*') { // multiline comment /* */
            advance(); advance();
            char c;
            while (!isAtEnd()) {
                c = peek();
                if (c == '*' && peekNext() == '/') {
                    advance(); advance();
                    break;
                }
                if (c == '\n') {
                    current->line++;
                }
                advance();
            }
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
  size_t length = current->current - current->tokenStart;
  for (Keyword *keyword = keywords; keyword->name != NULL; keyword++) {
    if (length == keyword->length &&
        memcmp(current->tokenStart, keyword->name, length) == 0) {
      type = keyword->type;
      break;
    }
  }

  if (type == TOKEN_IDENTIFIER && memcmp(current->tokenStart, "__LINE__", length) == 0) {
      Token token = makeToken(TOKEN_NUMBER);
      char *numBuf = calloc(8, 1);
      ASSERT_MEM(numBuf);
      sprintf(numBuf, "%d", current->line);
      token.start = numBuf;
      token.length = strlen(numBuf);
      token.lexeme = numBuf;
      return token;
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

static Token doubleQuotedString() {
  char last = '\0';
  while (!isAtEnd()) {
    if (peek() == '"' && last == '\\') {
        last = advance();
    } else if (peek() == '"') {
        break;
    } else {
        if (peek() == '\n') current->line++;
        last = advance();
    }
  }

  // Unterminated string.
  if (isAtEnd()) return errorToken("Unterminated string.");

  // The closing "
  advance();
  Token tok = makeToken(TOKEN_STRING_DQUOTE);

  // replace \" with "
  char *newBuf = calloc(tok.length+1, 1);
  ASSERT_MEM(newBuf);
  strncpy(newBuf, tok.start, tok.length);
  strReplace(newBuf, "\\\"", '"');
  strReplace(newBuf, "\\n", '\n');
  strReplace(newBuf, "\\t", '\t');
  strReplace(newBuf, "\\r", '\r');

  /*char *begInterp = NULL;*/
  /*char *beforeStart =  newBuf;*/
  /*while ((begInterp = strstr(beforeStart, "{$")) != NULL) {*/
      /*char *beforeEnd = begInterp-1;*/
      /*char *interpEnd = strstr(begInterp, "}");*/
      /*if (interpEnd == NULL) {*/
          /*return errorToken("Unterminated string interpolation");*/
      /*}*/
      /*beforeStart = interpEnd+1;*/
  /*}*/

  tok.start = newBuf;
  tok.length = strlen(newBuf);
  tok.lexeme = newBuf;
  if (CLOX_OPTION_T(debugTokens)) {
      fprintf(stderr, "  after replacements: '%s'\n", newBuf);
  }
  return tok;
}

static Token singleQuotedString(bool isStatic) {
    char last = '\0';
    while (!isAtEnd()) {
        if (peek() == '\'' && last == '\\') {
            last = advance();
        } else if (peek() == '\'') {
            break;
        } else {
            if (peek() == '\n') current->line++;
            last = advance();
        }
    }

    // Unterminated string.
    if (isAtEnd()) return errorToken("Unterminated string.");
    // The closing '
    advance();
    Token tok;
    if (isStatic) {
        tok = makeToken(TOKEN_STRING_STATIC);
    } else {
        tok = makeToken(TOKEN_STRING_SQUOTE);
    }

    // replace \" with "
    char *newBuf = calloc(tok.length+1, 1);
    ASSERT_MEM(newBuf);
    strncpy(newBuf, tok.start, tok.length);
    strReplace(newBuf, "\\\'", '\'');
    tok.start = newBuf;
    tok.length = strlen(newBuf);
    tok.lexeme = newBuf;
    if (CLOX_OPTION_T(debugTokens)) {
        fprintf(stderr, "  after replacements: '%s'\n", newBuf);
    }
    return tok;
}

Token scanToken(void) {
  skipWhitespace();

  current->tokenStart = current->current;
  if (isAtEnd()) return makeToken(TOKEN_EOF);

  char c = advance();

  if (isDigit(c)) return number();

  switch (c) {
    case '(': return makeToken(TOKEN_LEFT_PAREN);
    case ')': return makeToken(TOKEN_RIGHT_PAREN);
    case '{': {
        current->indent++;
        return makeToken(TOKEN_LEFT_BRACE);
    }
    case '}': {
        current->indent--;
        return makeToken(TOKEN_RIGHT_BRACE);
    }
    case '[': return makeToken(TOKEN_LEFT_BRACKET);
    case ']': return makeToken(TOKEN_RIGHT_BRACKET);
    case ';': return makeToken(TOKEN_SEMICOLON);
    case ':': return makeToken(TOKEN_COLON);
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

    case '"': return doubleQuotedString();
    case '\'': return singleQuotedString(false);
    case 's': {
        if (peek() == '\'') {
            advance();
            return singleQuotedString(true);
        }
        break;
    }
  }
  if (isAlpha(c)) return identifier();

  return errorToken("Unexpected character.");
}

void initScanner(Scanner *scan, const char *src) {
  scan->source = src;
  scan->tokenStart = src;
  scan->current = src;
  scan->line = 1;
  scan->indent = 0;
  scan->scriptEnded = false;
  setScanner(scan);
}

Scanner *getScanner(void) {
    return current;
}
void setScanner(Scanner *scan) {
    current = scan;
}

void resetScanner(Scanner *scan) {
  scan->tokenStart = scan->source;
  scan->current = scan->source;
  scan->line = 1;
  scan->indent = 0;
  scan->scriptEnded = false;
  setScanner(scan);
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
    case TOKEN_COLON:
      return "COLON";
    case TOKEN_SLASH:
      return "SLASH";
    case TOKEN_STAR:
      return "STAR";
    case TOKEN_IDENTIFIER:
      return "IDENTIFIER";
    case TOKEN_STRING_DQUOTE:
      return "DOUBLE_QUOTED_STRING";
    case TOKEN_STRING_SQUOTE:
      return "SINGLE_QUOTED_STRING";
    case TOKEN_STRING_STATIC:
      return "STATIC_STRING";
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
    case TOKEN_IN:
      return "IN";
    case TOKEN_BREAK:
      return "BREAK";
    case TOKEN_FOREACH:
      return "FOREACH";
    case TOKEN_CONTINUE:
      return "CONTINUE";
    case TOKEN_END_SCRIPT:
      return "__END__";
    case TOKEN_ERROR:
      return "!!ERROR!!";
    case TOKEN_EOF:
      return "EOF";
    default:
      return "!!UNKNOWN!!";
  }
}

void scanAllPrint(Scanner *scan, const char *src) {
    Scanner *oldCurrent = current;
    initScanner(scan, src);
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
    current = oldCurrent;
}

char *tokStr(Token *tok) {
    if (tok->lexeme != NULL) return tok->lexeme;
    char *buf = calloc(tok->length+1, 1);
    ASSERT_MEM(buf);
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
