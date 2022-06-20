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
  // new native should prevent GC from collect this one
  ObjNativeFn *fun = newNativeFn(function, fnname, arity);
  tableSet(&vm.globals, fnname, OBJ_VAL(OBJ_CAST(fun)));
}

// get/set for properties
void defineNativeProp(Value obj, NativeFn function, Value *vlu) {

}

// method built in on objects
void defineNativeFunProp(Value obj, NativeFn function, int arity);


void defineBuiltins() {
  // all builtin functions
  defineNativeFn("clock", clockNative, 0);
  defineNativeFn("str", toString, 1);
  defineNativeFn("num", toNumber, 1);
}