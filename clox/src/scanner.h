#ifndef CLOX_SCANNER_H
#define CLOX_SCANNER_H

typedef enum {
  // SINGLE CHAR
  TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
  TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
  TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
  TOKEN_SEMICOLON, TOKEN_COLON,
  TOKEN_SLASH, TOKEN_STAR,

  // one or more chars
  TOKEN_BANG, TOKEN_BANG_EQUAL,
  TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
  TOKEN_PLUS_EQUAL, TOKEN_MINUS_EQUAL,
  TOKEN_STAR_EQUAL, TOKEN_SLASH_EQUAL,
  TOKEN_GREATER, TOKEN_GREATER_EQUAL,
  TOKEN_LESS, TOKEN_LESS_EQUAL,
  //literals
  TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,
  // keywords
  TOKEN_AND, TOKEN_AS, TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_CLASS,
  TOKEN_ELSE, TOKEN_EXPORT, TOKEN_FALSE, TOKEN_FOR, TOKEN_FROM,
  TOKEN_FUN, TOKEN_IF, TOKEN_IMPORT, TOKEN_NIL, TOKEN_OR,
  TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS,
  TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE,

  TOKEN_ERROR, TOKEN_EOF
} TokenType;

typedef struct {
  TokenType type;
  const char *start;
  int length,
      line;
} Token;

// initalize scanner
void initScanner(const char *source);
// scan next token
Token scanToken();
// save state of current scanner
void scannerStashPush();
// restore scanner to saved state
bool scannerStashPop();
// peek forward, but don't advance
Token scanPeek(uint8_t distance);

extern const char *keywords[];
extern const size_t keywordCnt;

#endif // CLOX_SCANNER_H
