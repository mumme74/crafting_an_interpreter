#ifndef CLOX_COMPILER_H
#define CLOX_COMPILER_H

#include "common.h"
#include "object.h"
#include "vm.h"

// compiles source, returns containing function
ObjFunction *compile(const char *source);

// runned by GC during mark stage, before sweep
void markCompilerRoots(ObjFlags flags);

#endif // CLOX_COMPILER_H
