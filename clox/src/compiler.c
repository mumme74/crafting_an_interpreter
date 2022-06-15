#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "memory.h"

/*
(*grammar*)
program        -> statement* EOF ;
declaration    -> classDecl | funDecl | varDecl | statement ;
classDecl      -> "class" IDENTIFIER "{" function* "}" ;
funDecl        -> "fun" function ;
varDecl        -> "var" IDENTIFIER ( "=" expression )? ";" ;
statement      -> exprStmt
                | forStmt
                | ifStmt
                | printStmt
                | returnStmt
                | whileStmt
                | importStmt
                | exportStmt
                | block ;

function       -> IDENTIFIER "(" parameters? ")" block ;

exprStmt       -> expression ";" ;
forStmt        -> "for" "(" varDecl | exprStmt | ";" )
                  expression? ";"
                  expression? ";" ")" statement ;
ifStmt         -> "if" "(" expression ")" statement
                ( "else" statement )? ;
printStmt      -> "print" expression ";" ;
returnStmt     -> "return" expression? ";" ;
whileStmt      -> "while" "(" expression ")" statment ;
importStmt     -> "import" "{" importParam ("," importParam)* "}"
                     "from" STRING ";" ;
exportStmt     -> "export" dictDecl ;
block          -> "{" declaration* "}" ;

importParam    -> IDENTIFIER ( "as" IDENTIFIER)*

parameters     -> IDENTIFIER ( "," IDENTIFIER )* ;

expression     -> dictDecl | assignment ;
dictDecl       -> '{' (objectKeyValue (',' objectKeyValue*))* '}' ;
dictKeyValue   -> IDENTIFIER ':' expression ;
assignment     -> ( call "." )? IDENTIFIER ("="|"+="|-=|*=|/=) assignment
                | logic_or ;
logic_or       -> logic_and ( "or" logic_and )* ;
logic_and      -> equality ( "and" equality )* ;

equality       -> comparison ( ( "!=" | "==" ) comparison )* ;
comparison     -> term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
term           -> factor ( ( "-" | "+" ) factor )* ;
factor         -> unary ( ( "/" | "*" ) unary )* ;
unary          -> ( "!" | "-" ) unary | call ;
call           -> primary ( "(" arguments? ")" | "." IDENTIFIER )* ;
arguments      -> expression ( "," expression )* ;
primary        -> "true" | "false" | "nil"
                | NUMBER | STRING
                | "(" expression ")"
                | IDENTIFIER
                | "super" "." IDENTIFIER ;
*/

#ifdef DEBUG_PRINT_CODE
# include "debug.h"
#endif

typedef struct Parser {
  Token current,
        previous;
  bool hadError,
       panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGMENT, // =
  PREC_OR,        // or
  PREC_AND,       // and
  PREC_EQUALITY,  // == !=
  PREC_COMPARISON,// < > <= >=
  PREC_TERM,      // + -
  PREC_FACTOR,    // * /
  PREC_UNARY,     // ! -
  PREC_CALL,      // . () []
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

// for parse table to make precedence work
typedef struct ParseRule {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

// a local variable on stack
typedef struct Local {
  Token name;
  int depth;
  bool isCaptured;
} Local;

// a upvalue (when a closure occurs)
typedef struct Upvalue {
  uint8_t index;
  bool isLocal;
} Upvalue;

// which type of function is is
typedef enum {
  TYPE_METHOD,
  TYPE_FUNCTION,
  TYPE_INITIALIZER,
  TYPE_SCRIPT
} FunctionType;

// this obj is one for each break och continue that needs patching
// when we now how big the loop is
typedef struct  PatchJump {
  int patchPos;
  struct PatchJump *next;
} PatchJump;

// this obj is one for each loop
typedef struct LoopJumps {
  PatchJump *patchContinue,
            *patchBreak;
  struct LoopJumps *next; // in case many nested loops
} LoopJumps;

// each function gets a compiler object
typedef struct Compiler {
  struct Compiler* enclosing;
  ObjFunction *function;
  FunctionType type;
  Local locals[UINT8_COUNT];
  Upvalue upvalues[UINT8_COUNT];
  LoopJumps *loopJumps;
  int localCount,
      scopeDepth;
} Compiler;

// used during parsing to determine if class is inherited
typedef struct ClassCompiler {
  struct ClassCompiler *enclosing;
  bool hasSuperclass;
} ClassCompiler;

Parser parser;
Compiler *current = NULL;
ClassCompiler *currentClass = NULL;

// ---------------------------------------------

static void emitBytes(uint8_t byte1, uint8_t byte2);
static void expression();
static void statement();
static void declaration();
static uint8_t makeConstant(Value value);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static void initCompiler(Compiler *compiler, FunctionType type);
static void namedVariable(Token name, bool canAssign);
static Token syntheticToken(const char* text);
static void variable(bool canAssign);
static void string(bool canAssign);

// set a new error at Token pos with error message
static void errorAt(Token *token, const char *message) {
  if (parser.panicMode) return;
  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // nothing
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

// same as error, but at current pos instead
static void errorAtCurrent(const char *message) {
  errorAt(&parser.current, message);
}

// set a new error at previous pos
static void error(const char *message) {
  errorAt(&parser.previous, message);
}

// returns the currently used chunk
static Chunk *currentChunk() {
  return &current->function->chunk;
}

// move provard in token list
static void advance() {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;

    errorAtCurrent(parser.current.start);
  }
}

// consume token or report error message
static void consume(TokenType type, const char *message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

// check if Token is of type
static bool check(TokenType type) {
  return parser.current.type == type;
}

// check and advance if tokenType check succeeds
static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

// emit a single byte to bytecode
static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

// emit 2 bytes to byteCode
static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

// emit a jump backward to bytecode, must be pacthed later
static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);

  int offset = currentChunk()->count - loopStart +2;
  if (offset > UINT16_MAX) error("Loop body too large.");

  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

// emit a jump forward to bytecode, must be patched later
static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  emitByte(0xFF);
  emitByte(0xFF);
  return currentChunk()->count - 2;
}

// emit a empty return statment explicit or implicit
static void emitNilReturn() {
  if (current->type == TYPE_INITIALIZER) {
    emitBytes(OP_GET_LOCAL, 0);
  } else {
    emitByte(OP_NIL);
  }

  emitByte(OP_RETURN);
}

// emit byteCode for constant value
static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}

// patch a jump when code size i known
static void patchJump(int offset) {
  // -2 to adjust for the byteCode for the jump offset itself
  int jump = currentChunk()->count - offset -2;

  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");
  }

  uint8_t *code = currentChunk()->code;
  code[offset] = (jump >> 8) &0xff;
  code[offset + 1] = jump & 0xff;
}

// creates a new identifier and adds to constants table
static uint8_t identifierConstant(Token *name) {
  return makeConstant(OBJ_VAL(OBJ_CAST(copyString(name->start, name->length))));
}

// check if idenfiers are equal
static bool identifiersEqual(Token *a, Token *b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

// lookup a local variable
static int resolveLocal(Compiler *compiler, Token *name) {
  for (int i = compiler->localCount -1; i >= 0; --i) {
    Local *local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

// add a new closure value
static int addUpvalue(Compiler *compiler, uint8_t index,
                      bool isLocal)
{
  int upvalueCount = compiler->function->upvalueCount;
  for (int i = 0; i < upvalueCount; ++i) {
    Upvalue *upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == UINT8_COUNT) {
    error("Too many closure variables in function.");
    return 0;
  }

  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

// lookup a closure value
static int resolveUpValue(Compiler *compiler, Token *name) {
  if (compiler->enclosing == NULL) return -1;

  int local = resolveLocal(compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint8_t)local, true);
  }

  int upvalue = resolveUpValue(compiler->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(compiler, (uint8_t)upvalue, false);
  }

  return -1;
}

// add a new local to current frame
static void addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }

  Local *local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = -1;
  local->isCaptured = false;
}

// declare a new variable ie: var tmp;
static void declareVariable() {
  if (current->scopeDepth == 0) return;

  Token *name = &parser.previous;
  for (int i = 0; i < current->localCount; ++i) {
    Local *local = & current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error("Already a variable with this name in this scope.");
    }
  }

  addLocal(*name);
}

// context switch for parser, to get the correct parse precedence
// . has higher precedence than =
static void parsePrecedence(Precedence precedence) {
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGMENT;
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }
}

// parse a variable
static uint8_t parseVariable(const char *errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  declareVariable();
  if (current->scopeDepth > 0) return 0;

  return identifierConstant(&parser.previous);
}

// mark a verable as initialized
static void markInitialized() {
  if (current->scopeDepth == 0) return;
  current->locals[current->localCount -1].depth =
    current->scopeDepth;
}

// define a variable ie the: = value part ov var v = value;
static void defineVariable(uint8_t global) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }
  emitBytes(OP_DEFINE_GLOBAL, global);
}

// parse a function arguments list
static uint8_t argumentList() {
  uint8_t argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();
      if (argCount == 255) {
        error("Can't have more than 255 arguments.");
      }
      argCount++;
    } while(match(TOKEN_COMMA));
  }

  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}

// parse a and statemen, ie: if (v == 0 and t == 1) ...
static void and_(bool canAssign) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  parsePrecedence(PREC_AND);

  patchJump(endJump);
}

// parse a or statement ie: if (v == 0 or t == 1) ...
static void or_(bool canAssign) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

// creates, checks and adds, a Value constant
// such as identifiers, number literals, strings literals etc.
static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}

// end a function frame (compiled chunk)
static ObjFunction *endCompiler() {
  if (current->function->chunk.count == 0 ||
      current->function->chunk.code[
          current->function->chunk.count-1] != OP_RETURN)
  {
    emitNilReturn();
  }
  ObjFunction *function = current->function;
#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), "code");
  }
#endif

  if (current->type != TYPE_SCRIPT)
    current = current->enclosing;
  return function;
}

// begin a new scope
static void beginScope() {
  current->scopeDepth++;
}

// end current scope
static void endScope() {
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount-1].depth >
           current->scopeDepth)
  {
    if (current->locals[current->localCount -1].isCaptured) {
      emitByte(OP_CLOSE_UPVALUE);
    } else {
      emitByte(OP_POP);
    }
    current->localCount--;
  }
}

// parse a expression
static void expression() {
  parsePrecedence(PREC_ASSIGMENT);
}

// parses a code block ie: { ... }
static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

// parses a function
static void function(FunctionType type) {
  Compiler compiler;
  initCompiler(&compiler, type);
  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      current->function->arity++;
      if (current->function->arity > 255) {
        errorAtCurrent("Can't have more than 255 parameters");
      }
      uint8_t constant = parseVariable("Expect parameter name.");
      defineVariable(constant);
    } while(match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
  consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
  block();

  ObjFunction *function = endCompiler();
  emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(OBJ_CAST(function))));

  for (int i = 0; i < function->upvalueCount; ++i) {
    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalues[i].index);
  }
}

// declare a class method
static void method() {
  consume(TOKEN_IDENTIFIER, "Expect method name.");
  uint8_t constant = identifierConstant(&parser.previous);

  FunctionType type = TYPE_METHOD;
  if (parser.previous.length == 4 &&
      memcmp(parser.previous.start, "init", 4) == 0)
  {
    type = TYPE_INITIALIZER;
  }
  function(type);
  emitBytes(OP_METHOD, constant);
}

// declare a class
static void classDeclaration() {
  consume(TOKEN_IDENTIFIER, "Expect class name.");
  Token className = parser.previous;
  uint8_t nameConstant = identifierConstant(&parser.previous);
  declareVariable();

  emitBytes(OP_CLASS, nameConstant);
  defineVariable(nameConstant);

  ClassCompiler classCompiler;
  classCompiler.enclosing = currentClass;
  classCompiler.hasSuperclass = false;
  currentClass = &classCompiler;

  if (match(TOKEN_LESS)) {
    consume(TOKEN_IDENTIFIER, "Expect superclass name.");
    variable(false);
    if (identifiersEqual(&className, &parser.previous)) {
      error("A class can't inherit from itself.");
    }

    beginScope();
    addLocal(syntheticToken("super"));
    defineVariable(0);

    namedVariable(className, false);
    emitByte(OP_INHERIT);
    classCompiler.hasSuperclass = true;
  }

  namedVariable(className, false);
  consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    method();
  }
  consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
  emitByte(OP_POP);

  if (classCompiler.hasSuperclass) {
    endScope();
  }

  currentClass = currentClass->enclosing;
}

// declare a function
static void funDeclaration() {
  uint8_t global = parseVariable("Expect function name");
  markInitialized();
  function(TYPE_FUNCTION);
  defineVariable(global);
}

// declare a variable ie. var v = 1;
static void varDeclaration() {
  uint8_t global = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitByte(OP_NIL);
  }

  consume(TOKEN_SEMICOLON,
          "Expect ';' after variable declaration.");

  defineVariable(global);
}

// expresions starts here
static void expressionStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(OP_POP);
}

// updates a previous set jump statment to correct position when
// code postition is known
static void patchLoopGotoJumps(PatchJump** jump, int pos) {
  PatchJump *jmp = *jump, *freeMe;
  uint8_t *code = currentChunk()->code;

  while (jmp != NULL) {
    int jump;

    // jump backwards
    if (pos < jmp->patchPos) {
      code[jmp->patchPos -1] = OP_LOOP;
      jump = jmp->patchPos - pos + 2;
    } else
      // -2 to adjust for the byteCode for the jump offset itself
      jump = pos - jmp->patchPos -2;

    assert(jump > 0);

    if (jump > UINT16_MAX)
      error("Too much code to jump over.");

    code[jmp->patchPos] = (jump >> 8) & 0xff;
    code[jmp->patchPos + 1] = jump & 0xff;

    freeMe = jmp;
    jmp = jmp->next;
    FREE(PatchJump, freeMe);
  }
}

// handles for statement
static void forStatement() {
  beginScope();

  LoopJumps loopJmp;
  loopJmp.patchBreak = loopJmp.patchContinue = NULL;
  loopJmp.next = current->loopJumps;
  current->loopJumps = &loopJmp;

  // for (var i = 0; ....)
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
  if (match(TOKEN_SEMICOLON)) {
    consume(TOKEN_SEMICOLON, "Expect ';' .");
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    expressionStatement();
  }

  int loopStart = currentChunk()->count;
  // for (..; i < 10; ....)
  int exitJump = -1;
  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

    // jump out of loop if condition is false
    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // condition
  }

  // for (...;...; i++)
  if (!match(TOKEN_RIGHT_PAREN)) {
    int bodyJump = emitJump(OP_JUMP);
    int incrementStart = currentChunk()->count;
    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    emitLoop(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJump);
  }

  statement();
  emitLoop(loopStart);

  // bail out on false condition
  if (exitJump != -1) {
    patchJump(exitJump);
    emitByte(OP_POP);
  }

  patchLoopGotoJumps(&loopJmp.patchContinue, loopStart);
  patchLoopGotoJumps(&loopJmp.patchBreak, currentChunk()->count);

  endScope();
  current->loopJumps = loopJmp.next;
}

// handles if statement
static void ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  emitByte(OP_POP);

  if (match(TOKEN_ELSE)) statement();
  patchJump(elseJump);
}

// handles print statment
static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}

static void returnStatement() {
  if (current->type == TYPE_SCRIPT) {
    error("Can't return from top-level code.");
  }

  if (match(TOKEN_SEMICOLON)) {
    emitNilReturn();
  } else {
    if (current->type == TYPE_INITIALIZER) {
      error("Can't return a value from an initializer.");
    }

    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    emitByte(OP_RETURN);
  }
}

// handles while statements
static void whileStatement() {
  LoopJumps loopJmp;
  loopJmp.patchBreak = loopJmp.patchContinue = NULL;
  loopJmp.next = current->loopJumps;
  current->loopJumps = &loopJmp;

  int loopStart = currentChunk()->count;
  consume(TOKEN_LEFT_PAREN, "Expect '(' after while.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");


  int endJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  emitLoop(loopStart);
  patchJump(endJump);
  patchLoopGotoJumps(&loopJmp.patchContinue, loopStart);
  patchLoopGotoJumps(&loopJmp.patchBreak, currentChunk()->count);
  emitByte(OP_POP);

  current->loopJumps = loopJmp.next;
}

// parses a import param ie: id1 as id in
// import {id1 as id} from "path.lox"
static void importParam() {
  consume(TOKEN_IDENTIFIER, "Expect IDENTIFIER in import statement.");
  uint8_t name = identifierConstant(&parser.previous);
  (void)name;
  if (check(TOKEN_AS)) {
    advance();
    consume(TOKEN_IDENTIFIER, "Expect IDENTIFIER as alias.");
  }
  // FIXME implement
  addLocal(parser.previous);
}

// parses a import statement, ie:
// import {id1 as id, id2} from "path.lox"
static void importStatement() {
  consume(TOKEN_LEFT_BRACE, "Expect '{' after import.");
  do {
    importParam();
    if (!check(TOKEN_COMMA)) break;
    else advance();
  } while(true);

  consume(TOKEN_RIGHT_BRACE, "Expect '}' in import statement.");
  consume(TOKEN_FROM, "Expect 'from' after import params.");
  advance();
  string(false);
  consume(TOKEN_SEMICOLON, "Expect ';' after path.");
}

// when a recoverable syntax error occurs,
// so we can get many syntax errors roprted at the same time.
static void syncronize() {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;
    switch (parser.current.type) {
    case TOKEN_CLASS:
    case TOKEN_FUN:
    case TOKEN_VAR:
    case TOKEN_FOR:
    case TOKEN_IF:
    case TOKEN_WHILE:
    case TOKEN_PRINT:
    case TOKEN_RETURN:
      return;
    default: ; // do nothing
    }

    advance();
  }
}

// create a break/continue jump data struct
// is used later for pathcing when loop is finished
static PatchJump *loopGotoJump(const char *errMsg) {
  if (current->loopJumps == NULL) {
    errorAtCurrent(errMsg);
    return NULL;
  }

  PatchJump *jump = (PatchJump*)ALLOCATE(PatchJump, 1);
  if (jump == NULL) {
    error("Could not allocate memory during parsing.");
    return NULL;
  }

  jump->next = NULL;
  jump->patchPos = emitJump(OP_JUMP);
  return jump;
}

// top level, it starts from here
static void declaration() {
  if (match(TOKEN_CLASS)) {
    classDeclaration();
  } else if (match(TOKEN_FUN)) {
    funDeclaration();
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode) syncronize();
}

// parse a statement
static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_IMPORT)) {
    importStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

// parse a number
static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

// escape strings such as \n \t
static int escapeString(char *to, const char *from, int len) {
  const char *start = to, *end = from + len, *prev = from - 1;

  while (from < end) {
    if (*from == '\\' && *prev != '\\'){
      switch (*(from +1)) {
      case 't': *to++ = '\t'; from += 2; prev += 2; continue;
      case 'r': *to++ = '\r'; from += 2; prev += 2; continue;
      case 'n': *to++ = '\n'; from += 2; prev += 2; continue;
      case '0': *to++ = '\0'; from += 2; prev += 2; continue;
      case 'b': *to++ = '\b'; from += 2; prev += 2; continue;
      case 'f': *to++ = '\f'; from += 2; prev += 2; continue;
      case '\\':*to++ = '\\'; from += 2; prev += 2; continue;
      default: *to++ = *(++from); ++from; prev +=2; continue;
      }
    } else
      *to++ = *from++;
    ++prev;
  }
  return to - start;
}

// parse a string
static void string(bool canAssign) {
  char *escStr = ALLOCATE(char, parser.previous.length-2);
  int len = escapeString(
    escStr, parser.previous.start+1, parser.previous.length-2);

  emitConstant(OBJ_VAL(OBJ_CAST(copyString(escStr, len))));
  FREE_ARRAY(char, escStr, parser.previous.length-2);
}

// returns which assigment set is used is: +=, -= ...
static OpCode mutate(bool canAssign)
{
  if (canAssign) {
    switch (parser.current.type) {
    case TOKEN_PLUS_EQUAL:  advance(); return OP_ADD;
    case TOKEN_MINUS_EQUAL: advance(); return OP_SUBTRACT;
    case TOKEN_STAR_EQUAL:  advance(); return OP_MULTIPLY;
    case TOKEN_SLASH_EQUAL: advance(); return OP_DIVIDE;
    default: break;
    }
  }

  return OP_NIL;
}

// get a variable previously declared
static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpValue(current, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else if ((arg = identifierConstant(&name)) != -1) {
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  } else
    return; // not found

  OpCode mutateCode = mutate(canAssign);
  if (mutateCode != OP_NIL) {
    emitBytes(getOp, arg);
    expression();
    emitByte(mutateCode);
    emitBytes(setOp, arg);
  } else if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(setOp, arg);
  } else {
    emitBytes(getOp, arg);
  }
}

// parse a variable
static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

// create a custom token, containing text
static Token syntheticToken(const char* text) {
  Token token;
  token.start = text;
  token.length = (int)strlen(text);
  return token;
}

// parse a break statement
static void break_(bool canAssign) {
  (void)canAssign;
  PatchJump *jump = loopGotoJump("Can't use break outside of loop.");
  if (jump) {
    jump->next = current->loopJumps->patchBreak;
    current->loopJumps->patchBreak = jump;
  }
}

// parse a continue statement
static void continue_(bool canAssign) {
  (void)canAssign;
  PatchJump *jump = loopGotoJump("Can't use continue outside of loop.");
  if (jump) {
    jump->next = current->loopJumps->patchContinue;
    current->loopJumps->patchContinue = jump;
  }
}

// parse a super statement
static void super_(bool canAssign) {
  if (currentClass == NULL) {
    error("Can't use 'super' outside of a class.");
  } else if (!currentClass->hasSuperclass) {
    error("Can't use 'super' in a class with no superclass.");
  }

  consume(TOKEN_DOT, "Expect '.' after 'super'.");
  consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
  uint8_t name = identifierConstant(&parser.previous);

  namedVariable(syntheticToken("this"), false);
  if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    namedVariable(syntheticToken("super"), false);
    emitBytes(OP_SUPER_INVOKE, name);
    emitByte(argCount);
  } else {
    namedVariable(syntheticToken("super"), false);
    emitBytes(OP_GET_SUPER, name);
  }
}

// parse a this statement
static void this_(bool canAssign) {
  (void)canAssign;

  if (currentClass == NULL) {
    error("Can't use 'this' outside of a class.");
    return;
  }

  variable(false);
}

// parse a grouping ie: (....)
static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "EXPECT ')' after expression");
}

// parse a unary, ie '!' or '-' in  '!true' or '-1'
static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  // compile the operand
  parsePrecedence(PREC_UNARY);

  // Emit the operator instruction
  switch (operatorType) {
  case TOKEN_BANG:  emitByte(OP_NOT); break;
  case TOKEN_MINUS: emitByte(OP_NEGATE); break;
  default: return;
  }
}

// parse a binary expression such as 1 < 2 or 1 == 1
static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule *rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence +1));

  switch (operatorType) {
  case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
  case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
  case TOKEN_GREATER:       emitByte(OP_GREATER); break;
  case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
  case TOKEN_LESS:          emitByte(OP_LESS); break;
  case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
  case TOKEN_PLUS:          emitByte(OP_ADD); break;
  case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
  case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
  case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
  default: return; // unreachable
  }
}

// call a function
static void call(bool canAssign) {
  uint8_t argCount = argumentList();
  emitBytes(OP_CALL, argCount);
}

// subscript access property dict[...] or array[1]
static void subscript(bool canAssign) {
  //Chunk *chunk = currentChunk();
  expression();
  //int exprPos = chunk->count-2;
  //uint8_t name = chunk->count-1;

  consume(TOKEN_RIGHT_BRACKET, "Expect ']'.");
/* FIXME finish special subscript operator
  OpCode mutateCode = mutate(canAssign);
  if (mutateCode != OP_NIL) {
    Chunk *chunk = currentChunk();
    int getObjPos = chunk->count - 2;
    emitByte(chunk->code[getObjPos]);
    emitByte(chunk->code[getObjPos+1]);
    emitBytes(OP_GET_PROPERTY, name);
    expression();
    emitByte(mutateCode);
    emitBytes(OP_SET_PROPERTY, name);
  } else if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(OP_SET_PROPERTY, name);
  } else if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    emitBytes(OP_INVOKE, name);
    emitByte(argCount);
  } else {
    emitBytes(OP_GET_PROPERTY, name);
  }*/
}

// '.' accessor for classes and dicts
static void dot(bool canAssign) {
  consume(TOKEN_IDENTIFIER, "Expect property after '.'.");
  uint8_t name = identifierConstant(&parser.previous);

  OpCode mutateCode = mutate(canAssign);
  if (mutateCode != OP_NIL) {
    Chunk *chunk = currentChunk();
    int getObjPos = chunk->count - 2;
    emitByte(chunk->code[getObjPos]);
    emitByte(chunk->code[getObjPos+1]);
    emitBytes(OP_GET_PROPERTY, name);
    expression();
    emitByte(mutateCode);
    emitBytes(OP_SET_PROPERTY, name);
  } else if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(OP_SET_PROPERTY, name);
  } else if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    emitBytes(OP_INVOKE, name);
    emitByte(argCount);
  } else {
    emitBytes(OP_GET_PROPERTY, name);
  }
}

// parse literal, ie: true, false, nil
static void literal(bool canAssign) {
  switch (parser.previous.type) {
  case TOKEN_FALSE: emitByte(OP_FALSE); break;
  case TOKEN_NIL:   emitByte(OP_NIL); break;
  case TOKEN_TRUE:  emitByte(OP_TRUE); break;
  default: return; // Unreachable
  }
}

// parse a dict
static void dict(bool canAssign) {
  emitByte(OP_DICT);
  while (parser.current.type == TOKEN_IDENTIFIER) {
    consume(TOKEN_IDENTIFIER, "Expect key.");
    uint8_t constant = identifierConstant(&parser.previous);
    consume(TOKEN_COLON, "Expect ':' after dict key.");
    expression();
    if (parser.current.type != TOKEN_RIGHT_BRACE)
      consume(TOKEN_COMMA, "Expect ',' between dict fields.");
    emitBytes(OP_DICT_FIELD, constant);
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after dict declaration");
}

static ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]      = {grouping,  call,   PREC_CALL},
  [TOKEN_RIGHT_PAREN]     = {NULL,      NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]      = {dict,      NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE]     = {NULL,      NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACKET]    = {NULL,      subscript, PREC_CALL},
  [TOKEN_RIGHT_BRACKET]   = {NULL,      NULL,   PREC_NONE},
  [TOKEN_COMMA]           = {NULL,      NULL,   PREC_NONE},
  [TOKEN_DOT]             = {NULL,      dot,    PREC_CALL},
  [TOKEN_MINUS]           = {unary,     binary, PREC_TERM},
  [TOKEN_PLUS]            = {NULL,      binary, PREC_TERM},
  [TOKEN_SEMICOLON]       = {NULL,      NULL,   PREC_NONE},
  [TOKEN_COLON]           = {NULL,      NULL,   PREC_NONE},
  [TOKEN_SLASH]           = {NULL,      binary, PREC_FACTOR},
  [TOKEN_STAR]            = {NULL,      binary, PREC_FACTOR},
  [TOKEN_BANG]            = {unary,     NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]      = {NULL,      binary, PREC_EQUALITY},
  [TOKEN_EQUAL]           = {NULL,      NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]     = {NULL,      binary, PREC_EQUALITY},
  [TOKEN_PLUS_EQUAL]      = {NULL,      NULL,   PREC_NONE},
  [TOKEN_MINUS_EQUAL]     = {NULL,      NULL,   PREC_NONE},
  [TOKEN_STAR_EQUAL]      = {NULL,      NULL,   PREC_NONE},
  [TOKEN_SLASH_EQUAL]     = {NULL,      NULL,   PREC_NONE},
  [TOKEN_GREATER]         = {NULL,      binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL]   = {NULL,      binary, PREC_COMPARISON},
  [TOKEN_LESS]            = {NULL,      binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]      = {NULL,      binary, PREC_COMPARISON},
  [TOKEN_IDENTIFIER]      = {variable,  NULL,   PREC_NONE},
  [TOKEN_STRING]          = {string,    NULL,   PREC_NONE},
  [TOKEN_NUMBER]          = {number,    NULL,   PREC_NONE},
  [TOKEN_AND]             = {NULL,      and_,   PREC_AND},
  [TOKEN_AS]              = {NULL,      NULL,   PREC_NONE},
  [TOKEN_CLASS]           = {NULL,      NULL,   PREC_NONE},
  [TOKEN_ELSE]            = {NULL,      NULL,   PREC_NONE},
  [TOKEN_FALSE]           = {literal,   NULL,   PREC_NONE},
  [TOKEN_FOR]             = {NULL,      NULL,   PREC_NONE},
  [TOKEN_FROM]            = {NULL,      NULL,   PREC_NONE},
  [TOKEN_FUN]             = {NULL,      NULL,   PREC_NONE},
  [TOKEN_IF]              = {NULL,      NULL,   PREC_NONE},
  [TOKEN_IMPORT]          = {NULL,      NULL,   PREC_NONE},
  [TOKEN_NIL]             = {literal,   NULL,   PREC_NONE},
  [TOKEN_OR]              = {NULL,      or_,    PREC_OR},
  [TOKEN_PRINT]           = {NULL,      NULL,   PREC_NONE},
  [TOKEN_RETURN]          = {NULL,      NULL,   PREC_NONE},
  [TOKEN_BREAK]           = {break_,    NULL,   PREC_NONE},
  [TOKEN_CONTINUE]        = {continue_, NULL,   PREC_NONE},
  [TOKEN_SUPER]           = {super_,    NULL,   PREC_NONE},
  [TOKEN_THIS]            = {this_,     NULL,   PREC_NONE},
  [TOKEN_TRUE]            = {literal,   NULL,   PREC_NONE},
  [TOKEN_VAR]             = {NULL,      NULL,   PREC_NONE},
  [TOKEN_WHILE]           = {NULL,      NULL,   PREC_NONE},
  [TOKEN_ERROR]           = {NULL,      NULL,   PREC_NONE},
  [TOKEN_EOF]             = {NULL,      NULL,   PREC_NONE},
};

// returns the parse precedence rule for this token
static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

// initializes the compiler
static void initCompiler(Compiler *compiler, FunctionType type) {
  assert(compiler != current && "Setting itself as enclosing compiler.");
  compiler->enclosing = current;
  compiler->function = NULL;
  compiler->type = type;
  compiler->localCount = compiler->scopeDepth = 0;
  compiler->function = newFunction();
  compiler->loopJumps = NULL;

  current = compiler;
  if (type != TYPE_SCRIPT) {
    current->function->name = copyString(
                                parser.previous.start,
                                parser.previous.length);
  }

  Local *local = &current->locals[current->localCount++];
  local->depth = 0;
  local->isCaptured = false;
  if (type != TYPE_FUNCTION) {
    local->name.start = "this";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }
}

static void initParser(Parser *parser) {
  parser->hadError = parser->panicMode = false;
  parser->current.length = parser->current.line =
    parser->previous.length = parser->previous.line = 0;
  parser->current.start = parser->previous.start = '\0';
  parser->current.type = parser->previous.type = TOKEN_EOF;
}

// ---------------------------------------------

ObjFunction *compile(const char *source) {
  initScanner(source);
  initParser(&parser);

  Compiler compiler;
  initCompiler(&compiler, TYPE_SCRIPT);

  advance();
  while (!match(TOKEN_EOF)) {
    declaration();
  }

  ObjFunction *function = endCompiler();
  current = NULL;
  return parser.hadError ? NULL : function;
}

void markCompilerRoots(ObjFlags flags) {
  Compiler *compiler = current;
  while (compiler != NULL) {
    markObject(OBJ_CAST(compiler->function), flags);
    compiler = compiler->enclosing;
  }

}