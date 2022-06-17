#include <stdio.h>
#include <stdlib.h>
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

void pushValueArray(ValueArray *array, Value value) {
  if (array->capacity < array->count + 1) {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->values = GROW_ARRAY(Value, array->values,
                              oldCapacity, array->capacity);
  }

  array->values[array->count++] = value;
}

bool popValueArray(ValueArray *array, Value *value) {
  if (array->count < 1) return false;
  memcpy(value, &array->values[array->count-1], sizeof(Value));
  --array->count;
  return true;
}

bool getValueArray(ValueArray *array, int index, Value *value) {
  if (index < 0) index = array->count + index; // get from back
  if (index >= array->count) return false;
  memcpy(value, &array->values[index], sizeof(Value));
  return true;
}

bool setValueArray(ValueArray *array, int index, Value *value) {
  if (index < 0) index = array->count + index; // get from back
  if (index >= array->count) return false;
  memcpy(&array->values[index], value, sizeof(Value));
  return true;
}

/*ObjString joinValueArray(ValueArray *array, ObjString sep) {
  // FIXME implement
}*/

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

bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
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
    typeOfObject(AS_OBJ(value));
  }
#else
  switch (value.type) {
  case VAL_BOOL: return "boolean";
  case VAL_NIL: return "nil";
  case VAL_NUMBER: return "number";
  case VAL_OBJ: return typeOfObject(AS_OBJ(value));
  }
#endif
  return "undefined";
}

ObjString *valueToString(Value value) {
#ifdef NAN_BOXING
  if (IS_BOOL(value)) {
    return copyString(
      AS_BOOL(value) ? "true" : "false",
      AS_BOOL(value) ? 4 : 5);
  } else if (IS_NIL(value)) {
    return copyString("nil", 3);
  } else if (IS_NUMBER(value)) {
    const char buf[30];
    sprintf(buf, "%g", AS_NUMBER(value));
    return copyString(buf, strlen(buf));
  } else if (IS_OBJ(value)) {
    return objectToString(value);
  }
#else
  switch (value.type) {
  case VAL_BOOL:
    return copyString(
      AS_BOOL(value) ? "true" : "false",
      AS_BOOL(value) ? 4 : 5);
  case VAL_NIL:
    return copyString("nil", 3);
  case VAL_NUMBER: {
    char buf[30];
    sprintf(buf, "%g", AS_NUMBER(value));
    return copyString(buf, strlen(buf));
  }
  case VAL_OBJ:
    return objectToString(value);
  }
#endif
  return NULL;
}
