#ifndef CLOX_COMPILER_H
#define CLOX_COMPILER_H

#include "common.h"
#include "object.h"
#include "vm.h"
#include "module.h"
#include "scanner.h"

typedef struct LoopJumps LoopJumps;


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
  TYPE_SCRIPT,
  TYPE_EVAL
} FunctionType;

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

// compiles source, returns containing function
ObjFunction *compile(const char *source, Module *module,
                     FunctionType fnType);
// create a compileEval
ObjFunction *compileEvalExpr(const char *source, Chunk *parentChunk);

// looks up upvalue in parent function based on upvalue index
// function get set to the function containing upvalueIndex as a local
// index is the upvalue index in function, gets set to local index in containg function
Local *getUpvalueByIndex(ObjFunction **function, int *index);

// looks up upvalue in parent function based variable name
// function get set to the function containing upvalueIndex as a local
// index is the upvalue index in function, gets set to local index in containg function
Local *getUpvalueFromName(ObjFunction **function, const char *name, int *index);

// runned by GC during mark stage, before sweep
void markCompilerRoots(ObjFlags flags);

#endif // CLOX_COMPILER_H
