#ifndef CLOX_COMPILER_H
#define CLOX_COMPILER_H

#include "common.h"
#include "object.h"
#include "vm.h"

ObjFunction *compile(const char *source);
void markCompilerRoots(ObjFlags flags);

#endif // CLOX_COMPILER_H
