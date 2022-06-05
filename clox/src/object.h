#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include "common.h"
#include "value.h"
#include "chunk.h"

#define OBJ_TYPE(value)            (AS_OBJ(value)->type)
#define OBJ_CAST(value)            (Obj*)(value)

#define IS_CLOSURE(value)          (isObjType(value, OB_CLOSURE))
#define IS_FUNCTION(value)         (isObjType(value, OBJ_FUNCTION))
#define IS_NATIVE(value)           (isObjType(value, OBJ_NATIVE))
#define IS_STRING(value)           (isObjType(value, OBJ_STRING))

#define AS_CLOSURE(value)          ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)         ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE_OBJ(value)       ((ObjNative*)AS_OBJ(value))
#define AS_NATIVE(value)           (AS_NATIVE_OBJ(value)->function)
#define AS_STRING(value)           ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)          (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_STRING,
  OBJ_UPVALUE
} ObjType;

struct Obj {
  ObjType type;
  struct Obj* next;
};

typedef struct ObjFunction {
  Obj obj;
  int arity,
      upvalueCount;
  Chunk chunk;
  ObjString *name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct ObjNative {
  Obj obj;
  NativeFn function;
  ObjString *name;
  int arity;
} ObjNative;

struct ObjString {
  Obj obj;
  int length;
  char *chars;
  uint32_t hash;
};

typedef struct ObjUpvalue {
  Obj obj;
  Value *location;
  Value closed;
  struct ObjUpvalue *next;
} ObjUpvalue;

typedef struct ObjClosure {
  Obj obj;
  ObjFunction *function;
  ObjUpvalue **upvalues;
  int upvalueCount;
} ObjClosure;

ObjClosure   *newClosure(ObjFunction *function);
ObjFunction  *newFunction();
ObjNative    *newNative(NativeFn function, ObjString *name, int arity);
ObjUpvalue   *newUpvalue(Value *slot);

ObjString    *takeString(char *chars, int length);
ObjString    *copyString(const char *chars, int length);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif // CLOX_OBJECT_H
