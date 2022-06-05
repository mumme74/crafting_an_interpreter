#include <stdlib.h>

#include "chunk.h"
#include "memory.h"
#include "vm.h"

void initChunk(Chunk *chunk) {
  chunk->count = chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  initValueArray(&chunk->constants);
}

void freeChunk(Chunk *chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
  freeValueArray(&chunk->constants);
  initChunk(chunk);
}

void writeChunk(Chunk *chunk, uint8_t byte, int line) {
  if (chunk->capacity < chunk->count +1) {
    int oldCapacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(oldCapacity);
    chunk->code = GROW_ARRAY(uint8_t, chunk->code,
      oldCapacity, chunk->capacity);
    chunk->lines = GROW_ARRAY(int, chunk->lines,
                              oldCapacity, chunk->capacity);
  }

  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  ++chunk->count;
}

int addConstant(Chunk *chunk, Value value) {
  push(value); // for GC

  // prevent to store twice
  for (int i = 0; i < chunk->constants.count; ++i) {
    if (valuesEqual(chunk->constants.values[i], value))
      return i;
  }

  writeValueArray(&chunk->constants, value);
  pop(); // for GC
  return chunk->constants.count - 1;
}
