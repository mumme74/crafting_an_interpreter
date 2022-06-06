#include <stdio.h>
#include <string.h>

#include "common.h"
#include "value.h"
#include "memory.h"
#include "object.h"

void initValueArray(ValueArray *array) {
  array->count = array->capacity = 0;
  array->values = NULL;
}

void freeValueArray(ValueArray *array) {
  FREE_ARRAY(Value, array->values, array->capacity);
  initValueArray(array);
}

void writeValueArray(ValueArray *array, Value value) {
  if (array->capacity < array->count + 1) {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->values = GROW_ARRAY(Value, array->values,
                              oldCapacity, array->capacity);
  }

  array->values[array->count++] = value;
}

bool valuesEqual(Value a, Value b) {
#ifdef NAN_BOXING
  if (IS_NUMBER(a) && IS_NUMBER(b)) {
    return AS_NUMBER(a) == AS_NUMBER(b);
  }
  return a == b;
#else
  if (a.type != b.type) return false;
  switch (a.type) {
  case VAL_BOOL:     return AS_BOOL(a) == AS_BOOL(b);
  case VAL_NIL:      return true;
  case VAL_NUMBER:   return AS_NUMBER(a) == AS_NUMBER(b);
  case VAL_OBJ:      return AS_OBJ(a) == AS_OBJ(b);
  default:           return false; // unreachable
  }
#endif
}

const char *typeofValue(Value value) {
#ifdef NAN_BOXING
   if (IS_BOOL(value)) {
    return "boolean";
  } else if (IS_NIL(value)) {
    return "nil";
  } else if (IS_NUMBER(value)) {
    return "number";
  } else if (IS_OBJ(value)) {
    typeofObject(AS_OBJ(value));
  }
#else
  switch (value.type) {
  case VAL_BOOL: return "boolean";
  case VAL_NIL: return "nil";
  case VAL_NUMBER: return "number";
  case VAL_OBJ: return typeofObject(AS_OBJ(value));
  }
#endif
  return "undefined";
}

void printValue(Value value) {
#ifdef NAN_BOXING
  if (IS_BOOL(value)) {
    printf(AS_BOOL(value) ? "true" : "false");
  } else if (IS_NIL(value)) {
    printf("nil");
  } else if (IS_NUMBER(value)) {
    printf("%g", AS_NUMBER(value));
  } else if (IS_OBJ(value)) {
    printObject(value);
  }
#else
  switch (value.type) {
  case VAL_BOOL:
    printf(AS_BOOL(value) ? "true" : "false");
    break;
  case VAL_NIL: printf("nil"); break;
  case VAL_NUMBER: printf("%g", AS_NUMBER(value)); break;
  case VAL_OBJ: printObject(value); break;
  }
#endif
}
