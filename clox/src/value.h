#ifndef VALUE_H
#define VALUE_H

#include "common.h"

typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
} ValueType;

typedef struct {
  union {
    bool boolean;
    double number;
  } as;
  ValueType type;
} Value;

#define IS_BOOL(value)       ((value).type == VAL_BOOL)
#define IS_NIL(value)        ((value).type == VAL_NIL)
#define IS_NUMBER(value)     ((value).type == VAL_NUMBER)

#define AS_BOOL(value)       ((value).as.boolean)
#define AS_NUMBER(value)     ((value).as.number)

#define BOOL_VAL(value)      ((Value) {{.boolean = (value)}, VAL_BOOL})
#define NIL_VAL              ((Value) {{.number = 0},        VAL_NIL})
#define NUMBER_VAL(value)    ((Value) {{.number = (value)},  VAL_NUMBER})


typedef struct {
  int count;
  int capacity;
  Value *values;
} ValueArray;


void initValueArray(ValueArray *array);
void freeValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value);
void printValue(Value value);

#endif // VALUE_H
