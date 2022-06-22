#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

// only use this to allocate Objects, not C primitives
#define ALLOCATE_OBJ(type, objectType) \
  (type*)allocateObject(sizeof(type), objectType)

#define OBJ_TYPE(value)            (AS_OBJ(value)->type)
#define OBJ_CAST(value)            (Obj*)(value)

#define IS_MODULE(value)           (isObjType(value, OBJ_MODULE))
#define IS_REFERENCE(value)        (isObjType(value, OBJ_REFERENCE))
#define IS_BOUND_METHOD(value)     (isObjType(value, OBJ_BOUND_METHOD))
#define IS_DICT(value)             (isObjType(value, OBJ_DICT))
#define IS_CLASS(value)            (isObjType(value, OBJ_CLASS))
#define IS_CLOSURE(value)          (isObjType(value, OBJ_CLOSURE))
#define IS_FUNCTION(value)         (isObjType(value, OBJ_FUNCTION))
#define IS_INSTANCE(value)         (isObjType(value, OBJ_INSTANCE))
#define IS_NATIVE_FN(value)        (isObjType(value, OBJ_NATIVE_FN))
#define IS_NATIVE_PROP(value)      (isObjType(value, OBJ_NATIVE_PROP))
#define IS_NATIVE_METHOD(value)    (isObjType(value, OBJ_NATIVE_METHOD))
#define IS_PROTOTYPE(value)        (isObjType(value, OBJ_PROTOTYPE))
#define IS_STRING(value)           (isObjType(value, OBJ_STRING))

#define AS_MODULE(value)           ((ObjModule*)AS_OBJ(value))
#define AS_REFERENCE(value)        ((ObjReference*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)     ((ObjBoundMethod*)AS_OBJ(value))
#define AS_DICT(value)             ((ObjDict*)AS_OBJ(value))
#define AS_CLASS(value)            ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value)          ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)         ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value)         ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE_FN(value)        ((ObjNativeFn*)AS_OBJ(value))
#define AS_NATIVE_PROP(value)      ((ObjNativeProp*)AS_OBJ(value))
#define AS_NATIVE_METHOD(value)    ((ObjNativeMethod*)AS_OBJ(value))
#define AS_PROTOTYPE(value)        ((ObjPrototype*)AS_OBJ(value))
#define AS_STRING(value)           ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)          (((ObjString*)AS_OBJ(value))->chars)

#define CSTRING_TO_VALUE(string, length)    ((Value) { \
        {.obj = takeString(string, length)}, VAL_OBJ})


// Object lags
#define GC_FLAGS                   0x08
#define GC_IS_MARKED               0x01
#define GC_IS_OLDER                0x02
#define GC_IS_MARKED_OLDER         0x04
#define GC_DONT_COLLECT            0x08

typedef struct Module Module;
typedef struct ObjPrototype ObjPrototype;

typedef enum {
  OBJ_PROTOTYPE,
  OBJ_BOUND_METHOD,
  OBJ_ARRAY,
  OBJ_DICT,
  OBJ_CLASS,
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_INSTANCE,
  OBJ_NATIVE_FN,
  OBJ_NATIVE_PROP,
  OBJ_NATIVE_METHOD,
  OBJ_STRING,
  OBJ_UPVALUE,
  OBJ_MODULE,
  OBJ_REFERENCE
} ObjType;


struct Obj {
  ObjType type;
  ObjFlags flags;
  const ObjPrototype *prototype;
  struct Obj* next;
};

struct ObjPrototype {
  Obj obj;
  struct ObjPrototype *prototype;
  Table propsNative,
        methodsNative;
};

typedef struct ObjFunction {
  Obj obj;
  int arity,
      upvalueCount;
  Chunk chunk;
  ObjString *name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value *args);
typedef void (*NativePropSet)(Value obj, Value *vlu);
typedef Value (*NativePropGet)(Value obj);
typedef Value (*NativeMethod)(Value obj, int argCount, Value *args);

typedef struct ObjNativeFn {
  Obj obj;
  NativeFn function;
  ObjString *name;
  int arity;
} ObjNativeFn;

typedef struct ObjNativeProp {
  Obj obj;
  NativePropGet getFn;
  NativePropSet setFn;
  ObjString *name;
} ObjNativeProp;

typedef struct ObjNativeMethod {
  Obj obj;
  NativeMethod method;
  int arity;
  ObjString *name;
} ObjNativeMethod;

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

// used to bridge between import from other modules
typedef struct ObjReference {
  Obj obj;
  ObjString *name;
  ObjModule *mod;
  Chunk *chunk;
  ObjClosure *closure;
  int index;
} ObjReference;

typedef struct ObjDict {
  Obj obj;
  Table fields;
} ObjDict;


void initObjectsModule();
void freeObjectsModule();

Obj* allocateObject(size_t size, ObjType type);

ObjPrototype   *newPrototype(ObjPrototype *inherits);
ObjBoundMethod *newBoundMethod(Value reciever, ObjClosure *method);
ObjDict        *newDict();
ObjClass       *newClass(ObjString *name);
ObjClosure     *newClosure(ObjFunction *function);
ObjFunction    *newFunction();
ObjInstance    *newInstance(ObjClass *klass);
ObjNativeFn    *newNativeFn(NativeFn function, ObjString *name, int arity);
ObjNativeMethod *newNativeMethod(NativeMethod function, ObjString *name, int arity);
ObjNativeProp  *newNativeProp(NativePropGet getFn, NativePropSet setFn, ObjString *name);
ObjUpvalue     *newUpvalue(Value *slot);
ObjModule      *newModule(Module *module);
ObjReference   *newReference(ObjString *name, ObjModule *module,
                             int index, Chunk *chunk);

// get function for reference
Value refGet(ObjReference *ref);
// set function for reference
void refSet(ObjReference *ref, Value value);

// takes chars intern them and return a ObjString
// vm takes ownership of chars
ObjString      *takeString(char *chars, int length);
// copies chars intern them and return ObjString
// vm does NOT own chars
ObjString      *copyString(const char *chars, int length);
// concat str1 with str2
ObjString      *concatString(const char *str1, const char *str2, int len1, int len2);
// add quotes to string ie. "..."
ObjString      *quoteString(ObjString *valueStr);
// returns type of object
const char     *typeOfObject(Obj* object);
// returns obj converted to string
ObjString *objectToString(Value value);
// lookups property in inheritance chain
Value objPropNative(Obj *obj, ObjString *name);
// lookups method in inheritance chain
Value objMethodNative(Obj *obj, ObjString *name);

// test if Object is of type
static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

extern ObjPrototype
  *objPrototype,
  *objStringPrototype,
  *objDictPrototype;

#include "array.h"

#endif // CLOX_OBJECT_H
