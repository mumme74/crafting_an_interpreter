#include "native.h"
#include "vm.h"

#include <stdlib.h>
#include <time.h>

// -----------------------------------------------------------

static Value clockNative(int argCount, Value *args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value toString(int argCount, Value *args)  {
  return OBJ_VAL(OBJ_CAST(valueToString(args[0])));
}

static Value toNumber(int argCount, Value *args) {
  ObjString *vlu = IS_STRING(args[0]) ? AS_STRING(args[0]) :
                    valueToString(args[0]);
  char *end = vlu->chars + vlu->length;
  double dvlu = strtod(vlu->chars, &end);
  return NUMBER_VAL(dvlu);
}

// ------------------------------------------------------------


void defineNativeFn(const char *name, NativeFn function, int arity) {
  ObjString *fnname = copyString(name, (int)strlen(name));
  push(OBJ_VAL(OBJ_CAST(fnname)));
  push(OBJ_VAL(OBJ_CAST(newNative(function, fnname, arity))));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}


void defineBuiltins() {
  // all builtin functions
  defineNativeFn("clock", clockNative, 0);
  defineNativeFn("str", toString, 1);
  defineNativeFn("num", toNumber, 1);
}