#ifndef VALUE_H
#define VALUE_H

#include <string.h>

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

#ifdef NAN_BOXING

#define SIGN_BIT          ((uint64_t)0x8000000000000000)
#define QNAN              ((uint64_t)0x7ffc000000000000)

#define TAG_NIL   1 // 01
#define TAG_FALSE 2 // 10
#define TAG_TRUE  3 // 11


typedef uint64_t Value;

#define IS_NUMBER(value)   (((value) & QNAN) != QNAN)
#define IS_NIL(value)      ((value) == NIL_VAL)
#define IS_BOOL(value)     (((value) | 1) == TRUE_VAL)
#define IS_OBJ(value) \
    (((value) &(QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_BOOL(value)     ((value) == TRUE_VAL)
#define AS_NUMBER(value)   valueToNum(value)
#define AS_OBJ(value) \
    ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

#define BOOL_VAL(b)        ((b) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL          ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL           ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define NIL_VAL            ((Value)(uint64_t)(QNAN | TAG_NIL))
#define NUMBER_VAL(num)    numToValue(num)
#define OBJ_VAL(obj) \
    (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

static inline double valueToNum(Value value) {
  double num;
  memcpy(&num, &value, sizeof(Value));
  return num;
}

static inline Value numToValue(double num) {
  Value value;
  memcpy(&value, &num, sizeof(double));
  return value;
}

#else

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

#endif

typedef struct ValueArray{
  int count;
  int capacity;
  Value *values;
} ValueArray;


void initValueArray(ValueArray *array);

void freeValueArray(ValueArray *array);
void pushValueArray(ValueArray *array, Value value);
bool popValueArray(ValueArray  *array, Value *value);
bool getValueArray(ValueArray  *array, int index, Value *value);
bool setValueArray(ValueArray *array, int index, Value *value);
ObjString joinValueArray(ValueArray *array, ObjString sep);
// checks if values are equal
bool valuesEqual(Value a, Value b);
// checks if value is false
bool isFalsey(Value value);
// returns type of value to string
const char *typeofValue(Value value);
// returns value to string
ObjString *valueToString(Value value);

#endif // VALUE_H
