#include <stdio.h>

#include "debug.h"
#include "value.h"
#include "object.h"

static int simpleInstruction(const char *name, int offset) {
  printf("%s\n", name);
  return offset + 1;
}

static int byteInstruction(const char *name, Chunk *chunk,
                           int offset)
{
  uint8_t slot = chunk->code[offset +1];
  printf("%-16s %4d\n", name, slot);
  return offset + 2;
}

static int jumpInstruction(const char *name, int sign,
                           Chunk *chunk, int offset)
{
  uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
  jump |= chunk->code[offset + 2];
  printf("%-16s %4d > %d,\n", name, offset, offset +3 + sign * jump);
  return offset +3;
}

static int constantInstruction(const char *name, Chunk *chunk,
                               int offset)
{
  uint8_t constant = chunk->code[offset + 1];
  printf("%-16s %4d '%s'\n", name, constant,
         valueToString(chunk->constants.values[constant])->chars);
  return offset + 2;
}

static int invokeInstruction(const char *name, Chunk *chunk,
                             int offset)
{
  uint8_t constant = chunk->code[offset +1],
          argCount = chunk->code[offset +2];
  printf("%-16s (%d args) %4d '", name, argCount, constant);
  printf("%s\n", valueToString(chunk->constants.values[constant])->chars);
  return offset +3;
}

static int importInstruction(const char *name, Chunk *chunk, int offset) {
  uint8_t nameInExport = chunk->code[offset +1],
          alias        = chunk->code[offset +2],
          varIdx       = chunk->code[offset +3];
  printf("%-16s (%s) -> (%s) localIdx:%d\n", name,
         AS_CSTRING(chunk->constants.values[nameInExport]),
         AS_CSTRING(chunk->constants.values[alias]), varIdx);
  return offset +4;
}

static int exportInstruction(const char *name, Chunk *chunk, int offset) {
   uint8_t nameInExport = chunk->code[offset +1],
          varIdx        = chunk->code[offset +2],
          upIdx         = chunk->code[offset +3];
  printf("%-16s (%s) localIdx%d upIdx:%d\n", name,
         AS_CSTRING(chunk->constants.values[nameInExport]),
         varIdx, upIdx);

  return offset +4;
}

// ----------------------------------------------------

void disassembleChunk(Chunk *chunk, const char *name) {
  printf("== %s ==\n", name);
  for (int offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(chunk, offset);
  }
}

int disassembleInstruction(Chunk *chunk, int offset) {
  printf("%04d ", offset);

  if (offset > 0 &&
      chunk->lines[offset] == chunk->lines[offset-1])
  {
    printf("   | ");
  } else {
    printf("%4d ", chunk->lines[offset]);
  }

  uint8_t instruction = chunk->code[offset];
  switch (instruction) {
  case OP_CONSTANT:
    return constantInstruction("OP_CONSTANT", chunk, offset);
  case OP_NIL:
    return simpleInstruction("OP_NIL", offset);
  case OP_TRUE:
    return simpleInstruction("OP_TRUE", offset);
  case OP_FALSE:
    return simpleInstruction("OP_FALSE", offset);
  case OP_POP:
    return simpleInstruction("OP_POP", offset);
  case OP_GET_LOCAL:
    return byteInstruction("OP_GET_LOCAL", chunk, offset);
  case OP_GET_REFERENCE:
    return byteInstruction("OP_GET_REFERENCE", chunk, offset);
  case OP_GET_GLOBAL:
    return constantInstruction("OP_GET_GLOBAL", chunk, offset);
  case OP_GET_UPVALUE:
    return byteInstruction("OP_GET_UPVALUE", chunk, offset);
  case OP_GET_PROPERTY:
    return constantInstruction("OP_GET_PROPERTY", chunk, offset);
  case OP_GET_INDEXER:
    return simpleInstruction("OP_GET_INDEXER", offset);
  case OP_GET_SUPER:
    return constantInstruction("OP_GET_SUPER", chunk, offset);
  case OP_DEFINE_GLOBAL:
    return constantInstruction("OP_DEFINE_GLOBAL",
                               chunk, offset);
  case OP_SET_LOCAL:
    return byteInstruction("OP_SET_LOCAL", chunk, offset);
  case OP_SET_REFERENCE:
    return byteInstruction("OP_SET_REFERENCE", chunk, offset);
  case OP_SET_GLOBAL:
    return constantInstruction("OP_SET_GLOBAL",
                               chunk, offset);
  case OP_SET_UPVALUE:
    return byteInstruction("OP_SET_UPVALUE", chunk, offset);
  case OP_SET_PROPERTY:
    return constantInstruction("OP_SET_PROPERTY", chunk, offset);
  case OP_SET_INDEXER:
    return simpleInstruction("OP_SET_INDEXER", offset);
  case OP_EQUAL:
    return simpleInstruction("OP_EQUAL", offset);
  case OP_GREATER:
    return simpleInstruction("OP_GREATER", offset);
  case OP_LESS:
    return simpleInstruction("OP_LESS", offset);
  case OP_ADD:
    return simpleInstruction("OP_ADD", offset);
  case OP_SUBTRACT:
    return simpleInstruction("OP_SUBTRACT", offset);
  case OP_MULTIPLY:
    return simpleInstruction("OP_MULTIPLY", offset);
  case OP_DIVIDE:
    return simpleInstruction("OP_DIVIDE", offset);
  case OP_NOT:
    return simpleInstruction("OP_NOT", offset);
  case OP_NEGATE:
    return simpleInstruction("OP_NEGATE", offset);
  case OP_PRINT:
    return simpleInstruction("OP_PRINT", offset);
  case OP_JUMP:
    return jumpInstruction("OP_JUMP", 1, chunk, offset);
  case OP_JUMP_IF_FALSE:
    return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
  case OP_LOOP:
    return jumpInstruction("OP_LOOP", -1, chunk, offset);
  case OP_CALL:
    return byteInstruction("OP_CALL", chunk, offset);
  case OP_INVOKE:
    return invokeInstruction("OP_INVOKE", chunk, offset);
  case OP_SUPER_INVOKE:
    return invokeInstruction("OP_SUPER_INVOKE", chunk, offset);
  case OP_CLOSURE: {
    offset++;
    uint8_t constant = chunk->code[offset++];
    printf("%-15s  %4d '%s'\n", "OP_CLOSURE",
           constant,
           valueToString(chunk->constants.values[constant])->chars);
    ObjFunction *function = AS_FUNCTION(
      chunk->constants.values[constant]);
    for (int j = 0; j < function->upvalueCount; ++j) {
      int isLocal = chunk->code[offset++];
      int index = chunk->code[offset++];
      printf("%04d    |                     %s %d\n",
            offset - 2, isLocal ? "local" : "upvalue", index);
    }

    return offset;
  }
  case OP_CLOSE_UPVALUE:
    return simpleInstruction("OP_CLOSE_UPVALUE", offset);
  case OP_RETURN:
    return simpleInstruction("OP_RETURN", offset);
  case OP_EVAL_EXIT:
    return simpleInstruction("OP_EVAL_EXIT", offset);
  case OP_CLASS:
    return constantInstruction("OP_CLASS", chunk, offset);
  case OP_INHERIT:
    return simpleInstruction("OP_INHERIT", offset);
  case OP_METHOD:
    return constantInstruction("OP_METHOD", chunk, offset);
  case OP_DEFINE_DICT:
    return simpleInstruction("OP_DEFINE_DICT", offset);
  case OP_DICT_FIELD:
    return constantInstruction("OP_DICT_FIELD", chunk, offset);
  case OP_DEFINE_ARRAY:
    return simpleInstruction("OP_DEFINE_ARRAY", offset);
  case OP_ARRAY_PUSH:
    return simpleInstruction("OP_ARRAY_PUSH", offset);
  case OP_IMPORT_MODULE:
    return constantInstruction("OP_IMPORT_MODULE", chunk, offset);
  case OP_IMPORT_VARIABLE:
    return importInstruction("OP_IMPORT_VARIABLE", chunk, offset);
  case OP_EXPORT:
    return exportInstruction("OP_EXPORT", chunk, offset);

  case _OP_END:
    printf("_OP_END it's a bug if this text shows!!\n");
    return offset;

  }

  printf("Unknown opcode %d\n", instruction);
  return offset + 1;
}
