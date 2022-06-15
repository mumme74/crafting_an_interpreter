#ifndef CLOX_VM_H
#define CLOX_VM_H

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"
#include "module.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef void (*DebugCB)();

typedef struct {
  ObjClosure *closure;
  uint8_t *ip;
  Value *slots;
} CallFrame;

typedef struct {
  CallFrame frames[FRAMES_MAX];
  int    frameCount;
  Value  stack[STACK_MAX];
  Value* stackTop;
  Table  strings;
  Module  *modules;
  Module  *currentModule;
  ObjString *initString;
  ObjUpvalue* openUpvalues;
  size_t infantBytesAllocated,
         olderBytesAllocated,
         infantNextGC,
         olderNextGC;
  Obj   *infantObjects,
        *olderObjects;
  int   grayCount,
        grayCapacity;
  Obj** grayStack;
  DebugCB debugCB;
} VM;

typedef enum InterpretResult {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

// initalize VM (Singleton, global)
void initVM();
// free memory from VM
void freeVM();
//InterpretResult interpret(const char *source);

// run interpreter on module
InterpretResult interpretVM(Module *module);

// add a module to vm
void addModuleVM(Module *module);

// get a module based from file path
Module *getModule(const char *path);

// remove module from VM and free it
void delModuleVM(Module *module);

// GC mark phase
void markRootsVM(ObjFlags flags);

// GC sweep phase
void sweepVM(ObjFlags flags);

// push a value onto stack
void push(Value value);

// pop a value from stack
Value pop();

#endif // CLOX_VM_H
