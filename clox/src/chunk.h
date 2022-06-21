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
  OP_GET_INDEXER,
  OP_GET_SUPER,
  OP_DEFINE_GLOBAL,
  OP_SET_LOCAL,
  OP_SET_GLOBAL,
  OP_SET_UPVALUE,
  OP_SET_PROPERTY,
  OP_SET_INDEXER,
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
  OP_DEFINE_DICT,
  OP_DICT_FIELD,
  OP_DEFINE_ARRAY,
  OP_ARRAY_PUSH,
  OP_IMPORT_MODULE,
  OP_IMPORT_LINK,
  OP_EXPORT,
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

// initializes code chunk
void initChunk(Chunk *chunk);
// free memeory from this chunk
void freeChunk(Chunk *chunk);
// write (add) a bytecode to chunk, line is line source
void writeChunk(Chunk *chunk, uint8_t byte, int line);
// patch (update) a bytecode at chunk in pos
void patchChunkPos(Chunk *Chunk, uint8_t byte, int pos);
// patch (upadate) a line for chunk in pos
void patchChunkLine(Chunk *chunk, int line, int pos);
// add a constant to chunk
int addConstant(Chunk *chunk, Value value);

#endif // CHUNK_H
