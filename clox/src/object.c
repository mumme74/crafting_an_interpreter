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
  printf("%p allocate %zu for %s\n", (void*)object, size, typeOfObject(object));
#endif

  return object;
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

static int functionToString(char **pbuf, ObjFunction *function) {
  int len;
  if (function->name == NULL) {
    *pbuf = ALLOCATE(char, len = 8);
    sprintf(*pbuf, "<script>");
  } else {
    len = function->name->length + 5;
    *pbuf = ALLOCATE(char, len);
    sprintf(*pbuf, "<fn %s>", function->name->chars);
  }
  return len;
}

static int arrayToString(char **pbuf, ObjArray *array) {
  ObjString *tmp = joinValueArray(&array->arr, copyString(",", 1));
  *pbuf = ALLOCATE(char, tmp->length +3);
  **pbuf = '[';
  memcpy((*pbuf)+1, tmp->chars, tmp->length);
  memcpy((*pbuf)+1+tmp->length, "]\0", 2);
  return tmp->length +2;
}

static int dictToString(char **pbuf, ObjDict *dict) {
  ValueArray keys = tableKeys(&dict->fields),
             parts;
  int len = 0, i = 0;
  char *buf;
  ObjString *tmp;

  initValueArray(&parts);

  for (int i = 0; i < keys.count; ++i) {
    Value key, value;
    if (!getValueArray(&keys, i, &key)) continue;
    if (tableGet(&dict->fields, AS_STRING(key), &value)) {
      len += AS_STRING(key)->length + 1;
      tmp = valueToString(value);
      if (IS_STRING(value))
        tmp = quoteString(tmp);
      len += tmp->length;
      pushValueArray(&parts, OBJ_VAL(OBJ_CAST(tmp)));
    }
  }

  *pbuf = ALLOCATE(char, len += 3);
  buf = *pbuf;
  *buf++ = '{';
  for (i = 0; i < parts.count; ++i) {
    if (i > 0) *buf++ = ',';
    tmp = AS_STRING(keys.values[i]);
    memcpy(buf, tmp->chars, tmp->length);
    buf += tmp->length;
    *buf++ = ':';
    tmp = AS_STRING(parts.values[i]);
    memcpy(buf, tmp->chars, tmp->length);
    buf += tmp->length;
  }
  *buf++ = '}';
  *buf++ = '\0';

  freeValueArray(&keys);
  freeValueArray(&parts);
  return len;
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

ObjArray *newArray() {
  ObjArray *array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
  initValueArray(&array->arr);
  return array;
}

ObjDict *newDict() {
  ObjDict *dict = ALLOCATE_OBJ(ObjDict, OBJ_DICT);
  initTable(&dict->fields);
  return dict;
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

ObjString *concatString(const char *str1, const char *str2, int len1, int len2) {
  char *heapChars = ALLOCATE(char, len1 + len2);
  memcpy(heapChars, str1, len1);
  memcpy(heapChars, str2, len2);
  return takeString(heapChars, len1 + len2);
}

ObjString *quoteString(ObjString *valueStr) {
  char *buf = ALLOCATE(char, valueStr->length + 3);
  *buf = '"';
  memcpy(buf+1, valueStr->chars, valueStr->length);
  memcpy((buf+1+valueStr->length), "\"\0", 2);
  return takeString(buf, valueStr->length+2);
}

const char *typeOfObject(Obj* object) {
  switch (object->type){
  case OBJ_BOUND_METHOD: return "bound method";
  case OBJ_ARRAY:        return "array";
  case OBJ_DICT:         return "dict";
  case OBJ_CLASS:        return "class";
  case OBJ_CLOSURE:      return "closure";
  case OBJ_FUNCTION:     return "function";
  case OBJ_INSTANCE:     return "instance";
  case OBJ_NATIVE:       return "function";
  case OBJ_STRING:       return "string";
  case OBJ_UPVALUE:      return "upvalue";
  }
  return "undefined";
}

ObjString *objectToString(Value value) {
  ObjString *ret = NULL;
  char *buf = NULL;
  int len;
  switch (OBJ_TYPE(value)) {
  case OBJ_BOUND_METHOD: {
    len = functionToString(&buf,
                AS_BOUND_METHOD(value)->methods->function);
    ret = copyString(buf, len);
   } break;
  case OBJ_ARRAY: {
    len = arrayToString(&buf, AS_ARRAY(value));
    ret = copyString(buf, len);
  } break;
  case OBJ_DICT: {
    len = dictToString(&buf, AS_DICT(value));
    ret = copyString(buf, len);
   } break;
  case OBJ_CLASS: {
    ObjClass *cls = AS_CLASS(value);
    len = cls->name->length + 8;
    buf = ALLOCATE(char, len);
    sprintf(buf, "<class %s>", AS_CLASS(value)->name->chars);
    ret = copyString(buf, len);
  } break;
  case OBJ_CLOSURE: {
    //printClosure(AS_CLOSURE(value)); break;
    len = functionToString(&buf, AS_CLOSURE(value)->function);
    ret = copyString(buf, len);
  } break;
  case OBJ_FUNCTION: {
    len = functionToString(&buf, AS_FUNCTION(value));
    ret = copyString(buf, len);
  } break;
  case OBJ_INSTANCE: {
    ObjInstance *instance = AS_INSTANCE(value);
    len = instance->klass->name->length + 11;
    buf = ALLOCATE(char, len);
    sprintf(buf, "<%s instance>", instance->klass->name->chars);
    ret = copyString(buf, len);
  } break;
  case OBJ_NATIVE: {
    ObjNative *native = AS_NATIVE_OBJ(value);
    len = native->name->length + 12;
    buf = ALLOCATE(char, len);
    sprintf(buf, "<native fn %s>", AS_NATIVE_OBJ(value)->name->chars);
    ret = copyString(buf, len);
   } break;
  case OBJ_STRING:
    ret = AS_STRING(value); break;
  case OBJ_UPVALUE:
    ret = copyString("<upvalue>", 9); break;
  }

  if (buf != NULL)
    FREE_ARRAY(char, buf, len);
  return ret;
}
