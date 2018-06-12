#ifndef clox_scanner_h
#define clox_scanner_h

typedef enum {
  // all AST nodes need a token, so this is just a
  // placeholder token type for nodes that don't need one.
  TOKEN_EMPTY,
  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE,
  TOKEN_RIGHT_BRACE,
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
  TOKEN_SLASH,
  TOKEN_STAR,

  TOKEN_IDENTIFIER,
  TOKEN_STRING,
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

  TOKEN_ERROR,
  TOKEN_EOF
} TokenType;

typedef struct {
  TokenType type;
  const char *start;
  char *lexeme;
  int length; // not including NULL byte
  int line;
} Token;

typedef struct {
  const char *source;
  const char *tokenStart;
  const char *current;
  int line;
} Scanner;

extern Scanner scanner; // global

// init/reinit scanner
void initScanner(const char *src);
const char *tokTypeStr(TokenType ttype);
Token scanToken();
void scanAllPrint(const char *src);

Token emptyTok(void);
char *tokStr(Token *tok);
Token *copyToken(Token *tok);

#endif
