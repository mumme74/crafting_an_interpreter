#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

#define OBJ_TYPE(value)            (AS_OBJ(value)->type)
#define OBJ_CAST(value)            (Obj*)(value)

#define ID_IMPORT(value)           (isObjType(value, OBJ_IMPORT_LINK))
#define IS_MODULE(value)           (isObjType(value, OBJ_MODULE))
#define IS_BOUND_METHOD(value)     (isObjType(value, OBJ_BOUND_METHOD))
#define IS_DICT(value)             (isObjType(value, OBJ_DICT))
#define IS_CLASS(value)            (isObjType(value, OBJ_CLASS))
#define IS_CLOSURE(value)          (isObjType(value, OBJ_CLOSURE))
#define IS_FUNCTION(value)         (isObjType(value, OBJ_FUNCTION))
#define IS_INSTANCE(value)         (isObjType(value, OBJ_INSTANCE))
#define IS_NATIVE(value)           (isObjType(value, OBJ_NATIVE))
#define IS_STRING(value)           (isObjType(value, OBJ_STRING))

#define AS_IMPORT(value)           ((ObjImportLink*)AS_OBJ(value))
#define AS_MODULE(value)           ((ObjModule*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)     ((ObjBoundMethod*)AS_OBJ(value))
#define AS_DICT(value)             ((ObjDict*)AS_OBJ(value))
#define AS_CLASS(value)            ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value)          ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)         ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value)         ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE_OBJ(value)       ((ObjNative*)AS_OBJ(value))
#define AS_NATIVE(value)           (AS_NATIVE_OBJ(value)->function)
#define AS_STRING(value)           ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)          (((ObjString*)AS_OBJ(value))->chars)

#define CSTRING_TO_VALUE(string, length)    ((Value) { \
        {.obj = takeString(string, length)}, VAL_OBJ})


// Object lags
#define GC_FLAGS                   0x07
#define GC_IS_MARKED               0x01
#define GC_IS_OLDER                0x02
#define GC_IS_MARKED_OLDER         0x04

typedef struct Module Module;

typedef enum {
  OBJ_BOUND_METHOD,
  OBJ_DICT,
  OBJ_CLASS,
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_INSTANCE,
  OBJ_NATIVE,
  OBJ_STRING,
  OBJ_UPVALUE,
  //OBJ_MODULE,
  //OBJ_IMPORT_LINK
} ObjType;

struct Obj {
  ObjType type;
  ObjFlags flags;
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

typedef struct ObjClass {
  Obj obj;
  ObjString *name;
  Table methods;
} ObjClass;

typedef struct ObjInstance {
  Obj obj;
  ObjClass *klass;
  Table fields;
} ObjInstance;

typedef struct ObjBoundMethod {
  Obj obj;
  Value reciever;
  ObjClosure *methods;
} ObjBoundMethod;

typedef struct ObjModule {
  Obj obj;
  Module *module;
} ObjModule;

typedef struct ObjImportLink {
  Obj obj;
  ObjModule *module;
} ObjImportLink;

typedef struct ObjDict {
  Obj obj;
  Table fields;
} ObjDict;


ObjBoundMethod *newBoundMethod(Value reciever, ObjClosure *method);
ObjDict        *newDict();
ObjClass       *newClass(ObjString *name);
ObjClosure     *newClosure(ObjFunction *function);
ObjFunction    *newFunction();
ObjInstance    *newInstance(ObjClass *klass);
ObjNative      *newNative(NativeFn function, ObjString *name, int arity);
ObjUpvalue     *newUpvalue(Value *slot);

ObjImportLink  *newImportLink(Value *fromModule, Value *exportName);
ObjModule      *newModule(Value *path);

ObjString      *takeString(char *chars, int length);
ObjString      *copyString(const char *chars, int length);
ObjString      *concatString(const char *str1, const char *str2, int len1, int len2);

const char     *typeOfObject(Obj* object);
ObjString *objectToString(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif // CLOX_OBJECT_H
