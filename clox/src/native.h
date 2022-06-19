#ifndef LOX_NATIVE_H
#define LOX_NATIVE_H

#include "common.h"
#include "value.h"
#include "object.h"

// define a new native function to vm
void defineNativeFn(const char *name, NativeFn function, int arity);

// define all default natives
void defineBuiltins();

#endif // LOX_NATIVE_H
