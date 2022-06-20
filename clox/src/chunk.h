#ifndef CHUNK_H
#define CHUNK_H

#include "common.h"
#include "value.h"

typedef struct Module Module;
typedef struct Compiler Compiler;

typedef enum {
  // Any change here must also be changed in Computed goto labels
  OP_CONSTANT,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_GET_LOCAL,
  OP_GET_GLOBAL,
  OP_GET_UPVALUE,
  OP_GET_PROPERTY,
  OP_GET_SUBSCRIPT,
  OP_GET_SUPER,
  OP_DEFINE_GLOBAL,
  OP_SET_LOCAL,
  OP_SET_GLOBAL,
  OP_SET_UPVALUE,
  OP_SET_PROPERTY,
  OP_SET_SUBSCRIPT,
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NOT,
  OP_NEGATE,
  OP_PRINT,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_CALL,
  OP_INVOKE,
  OP_SUPER_INVOKE,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
  OP_RETURN,
  OP_EVAL_EXIT,
  OP_THROW,
  //OP_TRY,
  //OP_CATCH,
  //OP_FINALLY,
  OP_CLASS,
  OP_INHERIT,
  OP_METHOD,
  OP_DICT,
  OP_DICT_FIELD
} OpCode;

typedef struct {
  int count;
  int capacity;
  ValueArray constants;
  uint8_t *code;
  int *lines;
  Module *module;
  Compiler *compiler;
} Chunk;

void initChunk(Chunk *chunk);
void freeChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte, int line);
int addConstant(Chunk *chunk, Value value);


#endif // CHUNK_H
