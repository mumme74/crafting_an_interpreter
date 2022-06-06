#include <stdio.h>
#include <string.h>

#include "object.h"
#include "memory.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
# ifndef DEBUG_LOG_GC_ALLOC
#   define DEBUG_LOG_GC_ALLOC 1
# endif
#endif

#define ALLOCATE_OBJ(type, objectType) \
  (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
  Obj *object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;
  object->flags = 0;

  object->next = vm.infantObjects;
  vm.infantObjects = object;

#if DEBUG_LOG_GC_ALLOC
  printf("%p allocate %zu for %s\n", (void*)object, size, typeofObject(object));
#endif

  return object;
}

static void printFunction(ObjFunction *function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<fn %s>", function->name->chars);
}

static ObjString *allocateString(char *chars, int length,
                                 uint32_t hash)
{
  ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
  push(OBJ_VAL(OBJ_CAST(string))); // for GC
  tableSet(&vm.strings, string, NIL_VAL);
  pop(); // for GC
  return string;
}

static uint32_t hashString(const char *key, int length) {
  // FNV-1a algorithm
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

// -----------------------------------------------------------

ObjBoundMethod* newBoundMethod(Value reciever,
                               ObjClosure* method)
{
  ObjBoundMethod *bound =
    ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
  bound->reciever = reciever;
  bound->methods = method;
  return bound;
}

ObjClass *newClass(ObjString *name) {
  ObjClass *klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
  klass->name = name;
  initTable(&klass->methods);
  return klass;
}

ObjClosure *newClosure(ObjFunction *function) {
  ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue*,
                                   function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; ++i) {
    upvalues[i] = NULL;
  }

  ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjFunction *newFunction() {
  ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  initChunk(&function->chunk);
  return function;
}

ObjInstance *newInstance(ObjClass *klass) {
  ObjInstance *instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
  instance->klass = klass;
  initTable(&instance->fields);
  return instance;
}

ObjNative *newNative(NativeFn function, ObjString *name, int arity) {
  ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  native->function = function;
  native->arity = arity;
  native->name = name;
  return native;
}

ObjUpvalue *newUpvalue(Value *slot) {
  ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  upvalue->closed = NIL_VAL;
  upvalue->location = slot;
  upvalue->next = NULL;
  return upvalue;
}

ObjString *takeString(char *chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString *interned = tableFindString(&vm.strings, chars, length, hash);

  if (interned != NULL) {
    FREE_ARRAY(char, chars, length +1);
    return interned;
  }

  return allocateString(chars, length, hash);
}

ObjString *copyString(const char *chars, int length) {
  uint32_t hash = hashString(chars, length);
  // intern string
  ObjString *interned = tableFindString(
    &vm.strings, chars, length, hash);
  if (interned != NULL) return interned;

  char *heapChars = ALLOCATE(char, length +1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';
  return allocateString(heapChars, length, hash);
}

const char *typeofObject(Obj* object) {
  switch (object->type){
  case OBJ_BOUND_METHOD: return "bound method";
  case OBJ_CLASS: return "class";
  case OBJ_CLOSURE: return "closure";
  case OBJ_FUNCTION: return "function";
  case OBJ_INSTANCE: return "instance";
  case OBJ_NATIVE: return "function";
  case OBJ_STRING: return "string";
  case OBJ_UPVALUE: return "upvalue";
  }
  return "undefined";
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
  case OBJ_BOUND_METHOD:
    printFunction(AS_BOUND_METHOD(value)->methods->function);
    break;
  case OBJ_CLASS:
    printf("<class %s>", AS_CLASS(value)->name->chars); break;
  case OBJ_CLOSURE:
    //printClosure(AS_CLOSURE(value)); break;
    printFunction(AS_CLOSURE(value)->function); break;
  case OBJ_FUNCTION:
    printFunction(AS_FUNCTION(value)); break;
  case OBJ_INSTANCE: {
    ObjInstance *instance = AS_INSTANCE(value);
    printf("<%s instance>", instance->klass->name->chars);
  } break;
  case OBJ_NATIVE:
    printf("<native fn %s>", AS_NATIVE_OBJ(value)->name->chars);
    break;
  case OBJ_STRING:
    printf("%s", AS_CSTRING(value)); break;
  case OBJ_UPVALUE:
    printf("upvalue"); break;
  }
}
