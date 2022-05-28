package com.craftinginterpreters.lox;

import java.util.Arrays;
import java.util.ArrayList;
import java.util.List;
import static com.craftinginterpreters.lox.TokenType.*;


/*
(*grammar*)
program        -> statement* EOF ;
declaration    -> funDecl | varDecl | statement ;
funDecl        -> "fun" function ;
varDecl        -> "var" IDENTIFIER ( "=" expression )? ";" ;
statement      -> exprStmt
                | forStmt
                | ifStmt
                | printStmt
                | returnStmt
                | whileStmt
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
block          -> "{" declaration* "}" ;

parameters     -> IDENTIFIER ( "," IDENTIFIER )* ;

expression     -> assignment ;
assignment     -> IDENTIFIER "=" assignment
                | logic_or ;
logic_or       -> logic_and ( "or" logic_and )* ;
logic_and      -> equality ( "and" equality )* ;

equality       -> comparison ( ( "!=" | "==" ) comparison )* ;
comparison     -> term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
term           -> factor ( ( "-" | "+" ) factor )* ;
factor         -> unary ( ( "/" | "*" ) unary )* ;
unary          -> ( "!" | "-" ) unary | call ;
call           -> primary ( "(" arguments? ")" )* ;
arguments      -> expression ( "," expression )* ;
primary        -> "true" | "false" | "nil"
                | NUMBER | STRING
                | "(" expression ")"
                | IDENTIFIER ;
*/

public class Parser {
  private static class ParseError extends RuntimeException {}

  private final List<Token> tokens;
  private int current = 0;

  Parser(List<Token> tokens) {
    this.tokens = tokens;
  }

  List<Stmt> parse() {
    List<Stmt> statements = new ArrayList<>();
    while (!isAtEnd()) {
      statements.add(declaration());
    }
    return statements;
  }

  private boolean isAtEnd() {
    return peek().type == EOF;
  }

  private Token peek() {
    return tokens.get(current);
  }

  private Token previous() {
    return tokens.get(current - 1);
  }

  private Token advance() {
    if (!isAtEnd()) current++;
    return previous();
  }

  private boolean check(TokenType type) {
    if (isAtEnd()) return false;
    return peek().type == type;
  }

  private ParseError error(Token token, String message) {
    Lox.error(token, message);
    return new ParseError();
  }

  private boolean match(TokenType... types) {
    for(TokenType type : types) {
      if (check(type)) {
        advance();
        return true;
      }
    }
    return false;
  }

  private Token consume(TokenType type, String message) {
    if (check(type)) return advance();

    throw error(peek(), message);
  }

  private void syncronize() {
    advance();
    while(!isAtEnd()) {
      if (previous().type == SEMICOLON) return;

      switch(peek().type) {
      case CLASS: case FOR: case FUN: case IF:
      case PRINT: case RETURN: case WHILE:
        return;
      default: break;
      }

      advance();
    }
  }


  // the parser functions from here on
  // program        -> statement* EOF ;


  // declaration    -> varDecl | statement ;
  private Stmt declaration() {
    try {
      if (match(FUN)) return function("function");
      if (match(VAR)) return varDeclaration();
      return statement();
    } catch (ParseError error) {
      syncronize();
      return null;
    }
  }


  // ifStmt         -> "if" "(" expression ")" statement
  // ( "else" statement )? ;
  private Stmt ifStatement() {
    consume(LEFT_PAREN, "Expect '(' after 'if'.");
    Expr condition = expression();
    consume(RIGHT_PAREN, "Expect ')' after if condition.");

    Stmt thenBranch = statement();
    Stmt elseBranch = match(ELSE) ? statement() : null;

    return new Stmt.If(condition, thenBranch, elseBranch);
  }

  // varDecl        -> "var" IDENTIFIER ( "=" expression )? ";" ;
  private Stmt varDeclaration() {
    Token name = consume(IDENTIFIER, "Expect ';' after variable declaration.");

    Expr initializer = match(EQUAL) ? expression() : null;

    consume(SEMICOLON, "Expect ';' after variable declaration");

    return new Stmt.Var(name, initializer);
  }

  private Stmt whileStatement() {
    consume(LEFT_PAREN, "Expect '(' after while.");
    Expr condition = expression();
    consume(RIGHT_PAREN, "Expect ')' after condition.");
    Stmt body = statement();

    return new Stmt.While(condition, body);
  }


  // statement      -> exprStmt
  //                 | forStmt
  //                 | ifStmt
  //                 | printStmt
  //                 | returnStmt
  //                 | whileStmt
  //                 | block ;
  private Stmt statement() {
    if (match(FOR)) return forStatement();
    if (match(IF)) return ifStatement();
    if (match(PRINT)) return printStatement();
    if (match(RETURN)) return returnStatement();
    if (match(WHILE)) return whileStatement();
    if (match(LEFT_BRACE)) return new Stmt.Block(block());

    return expressionStatement();
  }

  // forStmt        -> "for" "(" varDecl | exprStmt | ";" )
  //  expression? ";"
  //  expression? ";" ")" statement ;
  private Stmt forStatement() {
    consume(LEFT_PAREN, "Expect '(' after 'for'.");

    Stmt initializer =
        match(SEMICOLON) ? null :
            (match(VAR) ? varDeclaration() : expressionStatement());
    Expr condition = check(SEMICOLON) ? null : expression();
    consume(SEMICOLON, "Expect ';' after loop condition.");
    Expr increment = check(RIGHT_PAREN) ? null : expression();
    consume(RIGHT_PAREN, "Expect ')' after for clause.");

    Stmt body = statement();
    if (increment != null) {
      body = new Stmt.Block(
        Arrays.asList(
          body,
          new Stmt.Expression(increment)));
    }

    // make sure to allow for(;;) loop
    if (condition == null) condition = new Expr.Literal(true);
    body = new Stmt.While(condition, body);

    if (initializer != null) // when we have a for (var i=0 ....) init bofre running condition
      body = new Stmt.Block(Arrays.asList(initializer, body));

    return body;
  }




  // block          -> "{" declaration* "}" ;
  private List<Stmt> block() {
    List<Stmt> statements = new ArrayList<>();

    while (!check(RIGHT_BRACE) && !isAtEnd()) {
      statements.add(declaration());
    }

    consume(RIGHT_BRACE, "Expect '}' after block.");
    return statements;
  }

  // exprStmt       -> expression ";" ;
  private Stmt expressionStatement() {
    Expr expr = expression();
    consume(SEMICOLON, "Expect ';' after expression.");
    return new Stmt.Expression(expr);
  }

  // function       -> IDENTIFIER "(" parameters? ")" block ;
  private Stmt.Function function(String kind) {
    Token name = consume(IDENTIFIER, "Expect " + kind + " name.");
    consume(LEFT_PAREN, "Expect '(' after " + kind + " name.");
    List<Token> parameters = new ArrayList<>();
    if (!check(RIGHT_PAREN)) {
      do {
        if (parameters.size() >= 255) {
          error(peek(), "Cant' have more than 255 parameters.");
        }

        parameters.add(
          consume(IDENTIFIER, "Expect parameter name."));
      } while(match(COMMA));
    }

    consume(RIGHT_PAREN, "Expect ')' after parameters.");

    consume(LEFT_BRACE, "Expect '{' before " + kind + " body.");
    List<Stmt> body = block();

    return new Stmt.Function(name, parameters, body);
  }

  // printStmt      -> "print" expression ";" ;
  private Stmt printStatement() {
    Expr value = expression();
    consume(SEMICOLON, "Exepct ';' after value.");
    return new Stmt.Print(value);
  }

  // returnStmt     -> "return" expression? ";" ;
  private Stmt returnStatement() {
    Token keyword = previous();
    Expr value = check(SEMICOLON) ? null : expression();

    consume(SEMICOLON, "Expect ';' after return statement.");
    return new Stmt.Return(keyword, value);
  }


  //expression     -> assignment ;
  private Expr expression() {
    return assignment();
  }

  //  assignment     -> IDENTIFIER "=" assignment
  //                 | logic_or ;
  private Expr assignment() {
    Expr expr = or();

    if (match(EQUAL)) {
      Token equals = previous();
      Expr value = assignment();

      if (expr instanceof Expr.Variable) {
        Token name = ((Expr.Variable)expr).name;
        return new Expr.Assign(name, value);
      }

      error(equals, "Invalid assignment target");
    }

    return expr;
  }

  // logic_or       -> logic_and ( "or" logic_and )* ;
  private Expr or() {
    Expr expr = and();

    while (match(OR)) {
      Token operator = previous();
      Expr right = and();
      expr = new Expr.Logical(expr, operator, right);
    }

    return expr;
  }

  // logic_and      -> equality ( "and" equality )* ;
  private Expr and() {
    Expr expr = equality();

    while (match(AND)) {
      Token operator = previous();
      Expr right = equality();
      expr = new Expr.Logical(expr, operator, right);
    }

    return expr;
  }

  // equality       -> comparison ( ( "!=" | "==" ) comparison )* ;
  private Expr equality() {
    Expr expr = comparison();
    while (match(BANG_EQUAL, EQUAL_EQUAL)) {
      Token operator = previous();
      Expr right = comparison();
      expr = new Expr.Binary(expr, operator, right);
    }

    return expr;
  }

  // comparison     -> term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
  private Expr comparison() {
    Expr expr = term();
    while (match(GREATER, GREATER_EQUAL, LESS, LESS_EQUAL)) {
      Token operator = previous();
      Expr right = term();
      expr = new Expr.Binary(expr, operator, right);
    }

    return expr;
  }

  // term           -> factor ( ( "-" | "+" ) factor )* ;
  private Expr term() {
    Expr expr = factor();

    while (match(MINUS, PLUS)) {
      Token operator = previous();
      Expr right = factor();
      expr = new Expr.Binary(expr, operator, right);
    }

    return expr;
  }

  // factor         -> unary ( ( "/" | "*" ) unary )* ;
  private Expr factor() {
    Expr expr = unary();

    while (match(SLASH, STAR)) {
      Token operator = previous();
      Expr right = unary();
      expr = new Expr.Binary(expr, operator, right);
    }

    return expr;
  }

  // unary          -> ( "!" | "-" ) unary | call ;
  private Expr unary() {
    if (match(BANG, MINUS)) {
      Token operator = previous();
      Expr right = unary();
      return new Expr.Unary(operator, right);
    }

    return call();
  }

  // call           -> primary ( "(" arguments? ")" )* ;
  private Expr call() {
    Expr expr = primary();

    while (true) {
      if (match(LEFT_PAREN))
        expr = finishCall(expr);
      else
        break;
    }

    return expr;
  }

  private Expr finishCall(Expr callee) {
    List<Expr> arguments = new ArrayList<>();
    if (!check(RIGHT_PAREN)) {
      do {
        if (arguments.size() >= 255)
          error(peek(), "Can't have more than 255 arguments");
        arguments.add(expression());
      } while(match(COMMA));
    }

    Token paren = consume(
      RIGHT_PAREN, "Expect ')' after arguments.");

    return new Expr.Call(callee, paren, arguments);
  }

  // primary        -> "true" | "false" | "nil"
  //                 | NUMBER | STRING
  //                 | "(" expression ")"
  //                 | IDENTIFIER ;
  private Expr primary() {
    if (match(FALSE)) return new Expr.Literal(false);
    if (match(TRUE)) return new Expr.Literal(true);
    if (match(NIL)) return new Expr.Literal(null);

    if (match(NUMBER, STRING)) {
      return new Expr.Literal(previous().literal);
    }

    if (match(IDENTIFIER))
      return new Expr.Variable(previous());

    if (match(LEFT_PAREN)) {
      Expr expr = expression();
      consume(RIGHT_PAREN, "Expect ')' after expression");
      return new Expr.Grouping(expr);
    }

    throw error(peek(), "Expect expression.");
  }
}
