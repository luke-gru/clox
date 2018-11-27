#ifndef clox_scanner_h
#define clox_scanner_h

#include <stdbool.h>

typedef enum {
  // all AST nodes require a token, so this is just a
  // placeholder token type for nodes that don't need one.
  TOKEN_EMPTY = 1,
  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE,
  TOKEN_RIGHT_BRACE,
  TOKEN_LEFT_BRACKET,
  TOKEN_RIGHT_BRACKET,
  TOKEN_BANG,
  TOKEN_BANG_EQUAL,
  TOKEN_COMMA,
  TOKEN_DOT,
  TOKEN_EQUAL,
  TOKEN_EQUAL_EQUAL,
  TOKEN_GREATER,
  TOKEN_GREATER_EQUAL,
  TOKEN_LESS,
  TOKEN_LESS_EQUAL,
  TOKEN_MINUS,
  TOKEN_PLUS,
  TOKEN_SEMICOLON,
  TOKEN_COLON,
  TOKEN_SLASH,
  TOKEN_STAR,

  TOKEN_IDENTIFIER,
  TOKEN_STRING_SQUOTE,
  TOKEN_STRING_DQUOTE,
  TOKEN_STRING_STATIC,
  TOKEN_NUMBER,

  TOKEN_AND,
  TOKEN_CLASS,
  TOKEN_MODULE,
  TOKEN_ELSE,
  TOKEN_FALSE,
  TOKEN_FUN,
  TOKEN_FOR,
  TOKEN_TRY,
  TOKEN_CATCH,
  TOKEN_THROW,
  TOKEN_IF,
  TOKEN_NIL,
  TOKEN_OR,
  TOKEN_PRINT,
  TOKEN_RETURN,
  TOKEN_SUPER,
  TOKEN_THIS,
  TOKEN_TRUE,
  TOKEN_VAR,
  TOKEN_WHILE,
  TOKEN_CONTINUE,
  TOKEN_BREAK,
  TOKEN_IN,

  TOKEN_END_SCRIPT,
  TOKEN_ERROR,
  TOKEN_EOF
} TokenType;

typedef enum {
    FUNCTION_TYPE_NAMED = 1,
    FUNCTION_TYPE_ANON,
    FUNCTION_TYPE_METHOD,
    FUNCTION_TYPE_GETTER,
    FUNCTION_TYPE_SETTER,
    FUNCTION_TYPE_CLASS_METHOD,
} ParseFunctionType;

typedef struct {
  TokenType type;
  const char *start;
  char *lexeme; // lazily computed, could be NULL. See `tokStr()`
  int length; // not including NULL byte
  int line;
} Token;

typedef struct {
  const char *source;
  const char *tokenStart;
  const char *current;
  int line;
  int indent;
  bool scriptEnded; // seen `__END__` keyword
} Scanner;

extern Scanner scanner; // main scanner

// init/reinit scanner
void initScanner(Scanner *scan, const char *src);
void resetScanner(Scanner *scan);
const char *tokTypeStr(TokenType ttype);
Token scanToken(void);
void scanAllPrint(Scanner *scan, const char *src);
Scanner *getScanner(void);
void setScanner(Scanner *scan);

Token emptyTok(void);
char *tokStr(Token *tok);
Token *copyToken(Token *tok);

#endif
