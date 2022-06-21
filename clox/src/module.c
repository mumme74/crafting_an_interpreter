#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "module.h"
#include "memory.h"
#include "compiler.h"
#include "vm.h"

// ------------------------------------------------------

void initModule(Module *module) {
  module->closure = NULL;
  module->source = NULL;
  module->name = module->path = NULL;
  module->rootFunction = NULL;
  module->closure = NULL;
  initTable(&module->exports);
}

void freeModule(Module *module) {
  //FREE(ObjString, module->name);
  //FREE(ObjString, module->path);
  FREE_ARRAY(char, (char*)module->source, strlen(module->source));
  //FREE(ObjClosure, module->closure);
  //FREE(ObjFunction, module->rootFunction);
  freeTable(&module->exports);
}

Module *createModule(const char *name, const char *path) {
  bool enabled = setGCenabled(false);
  Module *module = ALLOCATE(Module, 1);
  initModule(module);
  module->name = copyString(name, strlen(name));
  //tableSet(&module->globals, copyString("__name__", 8),
  //         OBJ_VAL(OBJ_CAST(module->name)));
  if (path != NULL)
    module->path = copyString(path, strlen(path));
  addModuleVM(module);
  setGCenabled(enabled);
  return module;
}

// compile source into module
bool compileModule(Module *module, const char *source) {
  bool enabled = setGCenabled(false);
  //vm.currentModule = module;
  if (module->source)
    FREE_ARRAY(char, (char*)module->source,
               strlen(module->source));
  size_t len = strlen(source);
  // take a copy of source, prevents unintentional free
  char *src = ALLOCATE(char, len);
  memcpy(src, source, len);
  module->source = src;

  module->rootFunction = compile(source, module, TYPE_SCRIPT);
  setGCenabled(enabled);

  return module->rootFunction != NULL;
}

InterpretResult interpretModule(Module *module) {
  bool enabled = setGCenabled(false);
  //vm.currentModule = module;
  module->closure = newClosure(module->rootFunction);
  push(OBJ_VAL(OBJ_CAST(module->closure)));
  setGCenabled(enabled);
  return interpretVM(module);
}

InterpretResult loadModule(Module *module) {
  //vm.currentModule = module;
  const char *src = readFile(module->path->chars);

  if (!compileModule(module, src))
    return INTERPRET_COMPILE_ERROR;

  int oldexitAtFrame = vm.exitAtFrame;
  vm.exitAtFrame = vm.frameCount;
  InterpretResult res = interpretModule(module);
  vm.exitAtFrame = oldexitAtFrame;

  return res;
}

Value getModuleByPath(Value path) {
  Module *mod = vm.modules;
  for (; mod != NULL; mod = mod->next) {
    if (valuesEqual(path, OBJ_VAL((Obj*)mod->path)))
      return OBJ_VAL((Obj*)mod);
  }

  // not yet loaded
  PathInfo pNfo = parsePath(AS_CSTRING(path));
  if (fileExists(pNfo.path)) {
    Module *mod = ALLOCATE(Module, 1);
    initModule(mod);
    addModuleVM(mod);
    mod->path = copyString(pNfo.path, pNfo.pathLen);
    mod->name = copyString(pNfo.basename, pNfo.basenameLen);
    if (loadModule(mod) == INTERPRET_OK)
      return OBJ_VAL((Obj*)newModule(mod));
    // failed, remove from vm
    delModuleVM(mod);
  }

  return NIL_VAL;
}

Value getModuleByName(Value name) {
  Module *mod = vm.modules;
  for (; mod != NULL; mod = mod->next) {
    if (valuesEqual(OBJ_VAL((Obj*)mod->name), name)) {
      ObjModule *omod = newModule(mod);
      return OBJ_VAL((Obj*)omod);
    }
  }

  ObjString *path = concatString(AS_STRING(name)->chars, "lox",
                                 AS_STRING(name)->length, 3);
  return getModuleByPath(OBJ_VAL((Obj*)path));
}

void markRootsModule(Module *module, ObjFlags flags) {
  markObject(OBJ_CAST(module->name), flags);
  markObject(OBJ_CAST(module->path), flags);
  markTable(&module->exports, flags);
}

void sweepModule(Module *module, ObjFlags flags) {
  tableRemoveWhite(&module->exports, flags);
}