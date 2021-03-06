#ifndef LOX_MODULE_LOX
#define LOX_MODULE_LOX

#include "object.h"

typedef enum InterpretResult InterpretResult;

typedef struct Module {
  Table  exports;
  const char *source;
  ObjString *name, *path;
  ObjFunction *rootFunction;
  ObjClosure *closure;
  Module *next;
} Module;

// create a new module, reciever takes ownership
Module *createModule(const char *name, const char *path);

// compile source into module
bool compileModule(Module *module, const char *source);

// run a compiled module
InterpretResult interpretModule(Module *module);

// load from file at path into module
InterpretResult loadModule(Module *module);

Value getModuleByPath(Value path);
Value getModuleByName(Value name);

// init module
void initModule(Module *module);
// free module stuff
void freeModule(Module *module);
// mark during GC mark and sweep
void markRootsModule(Module *module, ObjFlags flags);
// GC sweep phase
void sweepModule(Module *module, ObjFlags flags);

#endif // LOX_MODULE_LOX