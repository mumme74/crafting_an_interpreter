#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "module.h"
#include "memory.h"
#include "compiler.h"
#include "vm.h"

// ------------------------------------------------------

static char *readFile(ObjString *path) {
  FILE* file = fopen(path->chars, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path->chars);
    exit(74);
  }
  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char *buffer = ALLOCATE(char, fileSize +1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path->chars);
    exit(74);
  }
  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path->chars);
    exit(74);
  }

  buffer[bytesRead] = '\0';

  fclose(file);
  return buffer;
}

// ------------------------------------------------------

Module *createModule(const char *name) {
  bool enabled = setGCenabled(false);
  Module *module = ALLOCATE(Module, 1);
  initModule(module);
  module->name = copyString(name, strlen(name));
  tableSet(&module->globals, copyString("__name__", 8),
           OBJ_VAL(OBJ_CAST(module->name)));
  addModuleVM(module);
  setGCenabled(enabled);
  return module;
}

// compile source into module
bool compileModule(Module *module, const char *source) {
  bool enabled = setGCenabled(false);
  vm.currentModule = module;
  if (module->source) FREE(char, (char*)module->source);
  size_t len = strlen(source);
  // take a copy of source, prevents unintentional free
  char *src = ALLOCATE(char, len);
  memcpy(src, source, len);
  module->source = src;

  module->rootFunction = compile(source);
  setGCenabled(enabled);

  return module->rootFunction != NULL;
}

InterpretResult interpretModule(Module *module) {
  bool enabled = setGCenabled(false);
  vm.currentModule = module;
  module->closure = newClosure(module->rootFunction);
  push(OBJ_VAL(OBJ_CAST(module->closure)));
  setGCenabled(enabled);

  return interpretVM(module);
}

InterpretResult loadModule(Module *module, const char *path) {
  if (module->path) FREE(ObjString, module->path);
  vm.currentModule = module;
  module->path = copyString(path, strlen(path));

  const char *src = readFile(module->path);

  if (!compileModule(module, src))
    return INTERPRET_COMPILE_ERROR;

  return interpretModule(module);
}

void initModule(Module *module) {
  module->closure = NULL;
  module->source = NULL;
  module->name = module->path = NULL;
  module->rootFunction = NULL;
  module->closure = NULL;
  initTable(&module->globals);
  initTable(&module->exports);
}

void freeModule(Module *module) {
  //FREE(ObjString, module->name);
  //FREE(ObjString, module->path);
  FREE(char, (char*)module->source);
  //FREE(ObjClosure, module->closure);
  //FREE(ObjFunction, module->rootFunction);
  freeTable(&module->globals);
  freeTable(&module->exports);
}

void markRootsModule(Module *module, ObjFlags flags) {
  markObject(OBJ_CAST(module->name), flags);
  markObject(OBJ_CAST(module->path), flags);
  markTable(&module->globals, flags);
  markTable(&module->exports, flags);
}

void sweepModule(Module *module, ObjFlags flags) {
  tableRemoveWhite(&module->exports, flags);
  tableRemoveWhite(&module->globals, flags);
}