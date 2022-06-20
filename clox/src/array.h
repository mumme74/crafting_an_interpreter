#ifndef LOX_ARRAY_H
#define LOX_ARRAY_H

#include "common.h"
#include "object.h"
#include "value.h"

// all array object API stuff in this module

#define IS_ARRAY(value)            (isObjType(value, OBJ_ARRAY))
#define AS_ARRAY(value)            ((ObjArray*)AS_OBJ(value))


typedef struct ObjArray {
  Obj obj;
  ValueArray arr;
} ObjArray;


void initArrayModule();
void freeArrayModule();

ObjArray *newArray();
Value lenArray(const Value array);
Value pushArray(Value array, int argCount, Value *args);
Value popArray(Value array, int argCount, Value *args);
Value toStringArray(Value array);


extern ObjPrototype *objArrayPrototype;

#endif // LOX_ARRAY_H
