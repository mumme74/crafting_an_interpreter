#ifndef VALUE_H
#define VALUE_H

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_OBJ,
} ValueType;

typedef struct Value {
  union {
    bool boolean;
    double number;
    Obj *obj;
  } as;
  ValueType type;
} Value;

#define IS_BOOL(value)       ((value).type == VAL_BOOL)
#define IS_NIL(value)        ((value).type == VAL_NIL)
#define IS_NUMBER(value)     ((value).type == VAL_NUMBER)
#define IS_OBJ(value)        ((value).type == VAL_OBJ)

#define AS_BOOL(value)       ((value).as.boolean)
#define AS_NUMBER(value)     ((value).as.number)
#define AS_OBJ(value)        ((value).as.obj)

#define BOOL_VAL(value)      ((Value) {{.boolean = (value)}, VAL_BOOL})
#define NIL_VAL              ((Value) {{.number = 0},        VAL_NIL})
#define NUMBER_VAL(value)    ((Value) {{.number = (value)},  VAL_NUMBER})
#define OBJ_VAL(value)       ((Value) {{.obj = value},       VAL_OBJ})


typedef struct ValueArray{
  int count;
  int capacity;
  Value *values;
} ValueArray;


void initValueArray(ValueArray *array);
void freeValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value);
bool valuesEqual(Value a, Value b);
void printValue(Value value);

#endif // VALUE_H
