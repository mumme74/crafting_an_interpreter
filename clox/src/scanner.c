#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"
#include "memory.h"

typedef struct {
  const char* start;
  const char* current;
  int line;
} Scanner;

typedef struct StashScanner {
  Scanner scanner;
  struct StashScanner *next;
} StashScanner;

StashScanner *stashStack = NULL;

Scanner scanner;

// ------------------------------------------------------------

static bool isAtEnd() {
  return *scanner.current == '\0';
}

static char advance() {
  scanner.current++;
  return scanner.current[-1];
}

Token scanPeek(uint8_t distance) {
  const char *oldCur = scanner.current,
             *oldStart = scanner.start;
  int oldLine = scanner.line;

  for (int i = 0; i < distance-1 && !isAtEnd(); ++i)
    scanToken();
  Token tok = scanToken();

  scanner.current = oldCur;
  scanner.start = oldStart;
  scanner.line = oldLine;

  return tok;
}

static Token makeToken(TokenType type) {
  Token token;
  token.type = type;
  token.start = scanner.start;
  token.length = (int)(scanner.current - scanner.start);
  token.line = scanner.line;
  return token;
}

static Token makeTokenAdvance(TokenType type, int moveFw) {
  while (moveFw-- > 0) scanner.current++;
  return makeToken(type);
}

static Token errorToken(const char *message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = scanner.line;
  return token;
}


static bool match(char expected) {
  if (isAtEnd()) return false;
  if (*scanner.current != expected) return false;
  scanner.current++;
  return true;
}

static char peek() {
  return *scanner.current;
}

static char peekNext() {
  if (isAtEnd()) return '\0';
  return scanner.current[1];
}

static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
          c == '_';
}

static const char *skipWhitespace() {
  for (;;) {
    char c = peek();
    switch (c) {
    case ' ': case '\r': case '\t':
      advance(); break;
    case '\n':
      scanner.line++;
      advance(); break;
    case '/':
      if (peekNext() == '/') {
        // a comment
        while (peek() != '\n' && !isAtEnd())
          advance();
        break; // switch
      } else if (peekNext() == '*') {
        char prev = advance(); // the '*' in /*
        int nestCount = 0, startLine = scanner.line;
        const char *startPos = scanner.current+1;
        do {
          prev = c;
          c = advance();
          if (c == '\n') scanner.line++;
          else if (prev == '/' && c == '*')
            ++nestCount;
          else if (prev == '*' && c == '/')
            --nestCount;
          else if (isAtEnd()) {
            scanner.current = startPos;
            scanner.line = startLine;
            return "Unmatched '/*'.";
          }
        } while (nestCount > 0 && !isAtEnd());
        break; // switch
      } else
        return NULL;
    default: return NULL;
    }
  }
}

static Token string() {
  char c;
  while((c = peek()) != '"' && !isAtEnd()) {
    switch(c) {
    case '\n': ++scanner.line; advance(); break;
    case '\\':
      if (peekNext() == '"') scanner.current += 2;
      // fall through
    default: advance();
    }
  }

  if (isAtEnd()) return errorToken("Unterminated string.");

  // the closing quote
  advance();
  return makeToken(TOKEN_STRING);
}


static Token number() {
  while (isDigit(peek())) advance();

  // fractional part
  if (peek() == '.' && isDigit(peekNext())) {
    //consume '.'
    advance();

    while (isDigit(peek())) advance();
  }

  return makeToken(TOKEN_NUMBER);
}

static TokenType checkKeyword(int start, int length,
                    const char *rest, TokenType type)
{
  if (scanner.current - scanner.start == start + length &&
      memcmp(scanner.start + start, rest, length) == 0)
  {
    return type;
  }

  return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
  switch (scanner.start[0]) {
  case 'a':
    if (scanner.current - scanner.start > 1 &&
        scanner.start[1] == 's') return TOKEN_AS;
    else return checkKeyword(1, 2, "nd", TOKEN_AND);
  case 'b': return checkKeyword(1, 4, "reak", TOKEN_BREAK);
  case 'c':
    if (scanner.current - scanner.start > 1) {
      switch (scanner.start[1]) {
      case 'o': return checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE);
      case 'l': return checkKeyword(2, 3, "ass", TOKEN_CLASS);
      default: break;
      }
    }
  case 'e':
    if (scanner.current - scanner.start > 1 &&
        scanner.start[1] == 'x')
      return checkKeyword(2, 4, "port", TOKEN_EXPORT);
    else return checkKeyword(1, 3, "lse", TOKEN_ELSE);
  case 'f':
    if (scanner.current - scanner.start > 1) {
      switch (scanner.start[1]) {
      case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
      case 'r': return checkKeyword(2, 2, "om", TOKEN_FROM);
      case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
      case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
      default: break;
      }
    }
    break;
  case 'i':
    if (scanner.current - scanner.start > 1 &&
        scanner.start[1] == 'm')
        return checkKeyword(2, 4, "port", TOKEN_IMPORT);
    else return checkKeyword(1, 1, "f", TOKEN_IF);
  case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
  case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
  case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
  case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
  case 's': return checkKeyword(1, 4, "uper", TOKEN_SUPER);
  case 't':
    if (scanner.current - scanner.start > 1) {
      switch (scanner.start[1]) {
      case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
      case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
      default: break;
      }
    }
    break;
  case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
  case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
  default: break;
  }
  return TOKEN_IDENTIFIER;
}

static Token identifier() {
  while(isAlpha(peek()) || isDigit(peek())) advance();
  return makeToken(identifierType());
}

// ------------------------------------------------------------


void initScanner(const char *source) {
  scanner.start = scanner.current = source;
  scanner.line = 1;
}

void scannerStashPush() {
  StashScanner **prev = &stashStack;
  while (*prev != NULL)
    *prev = (*prev)->next;

  *prev = ALLOCATE(StashScanner, 1);
  memcpy(&(*prev)->scanner, &scanner, sizeof(scanner));
  (*prev)->next = NULL;
}

bool scannerStashPop() {
  if (stashStack == NULL) return false;

  StashScanner  **stsh = &stashStack,
                **prev = &stashStack;
  while (*stsh != NULL && (*stsh)->next != NULL) {
    *prev = *stsh;
    *stsh = (*stsh)->next;
  }
  if (*prev != stashStack)
    prev = &(*prev)->next;

  memcpy(&scanner, &(*stsh)->scanner, sizeof(scanner));
  FREE(StashScanner, *stsh);
  *prev = NULL;
  return true;
}

Token scanToken() {
  const char *failMsg = skipWhitespace();
  if (failMsg != NULL)
    return errorToken(failMsg);

  scanner.start = scanner.current;

  if (isAtEnd()) return makeToken(TOKEN_EOF);

  char c = advance();

  if (isAlpha(c)) return identifier();
  if (isDigit(c)) return number();

  switch(c) {
  case '(': return makeToken(TOKEN_LEFT_PAREN);
  case ')': return makeToken(TOKEN_RIGHT_PAREN);
  case '{': return makeToken(TOKEN_LEFT_BRACE);
  case '}': return makeToken(TOKEN_RIGHT_BRACE);
  case '[': return makeToken(TOKEN_LEFT_BRACKET);
  case ']': return makeToken(TOKEN_RIGHT_BRACKET);
  case ':': return makeToken(TOKEN_COLON);
  case ';': return makeToken(TOKEN_SEMICOLON);
  case ',': return makeToken(TOKEN_COMMA);
  case '.': return makeToken(TOKEN_DOT);
  case '-':
    if (peek() == '=') return makeTokenAdvance(TOKEN_MINUS_EQUAL, 1);
    else return makeToken(TOKEN_MINUS);
  case '+':
    if (peek() == '=') return makeTokenAdvance(TOKEN_PLUS_EQUAL, 1);
    else return makeToken(TOKEN_PLUS);
  case '*':
    if (peek() == '=') return makeTokenAdvance(TOKEN_STAR_EQUAL, 1);
    else return makeToken(TOKEN_STAR);
  case '/':
    if (peek() == '=') return makeTokenAdvance(TOKEN_SLASH_EQUAL, 1);
    else return makeToken(TOKEN_SLASH);
  case '!':
    return makeToken(
      match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
  case '=':
    return makeToken(
      match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
  case '<':
    return makeToken(
      match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
  case '>':
    return makeToken(
      match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
  case '"': return string();
  }

  return errorToken("Unexpected character.");
}

const char *keywords[] = {
  "and", "as", "break", "continue", "class", "else", "false",
  "for", "from", "fun", "if", "import", "nil", "or",
  "print", "return", "super", "this", "true", "var", "while"
};
const size_t keywordCnt = sizeof(keywords) / sizeof(keywords[0]);

