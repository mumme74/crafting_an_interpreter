#include "array.h"
#include "object.h"
#include "value.h"
#include "common.h"
#include "vm.h"
#include "memory.h"

// ---------------------------------------------------------------

static int arrayToString(char **pbuf, ObjArray *array) {
  ObjString *tmp = joinValueArray(&array->arr, copyString(",", 1));
  *pbuf = ALLOCATE(char, tmp->length +3);
  **pbuf = '[';
  memcpy((*pbuf)+1, tmp->chars, tmp->length);
  memcpy((*pbuf)+1+tmp->length, "]\0", 2);
  return tmp->length +2;
}

static Value getArrayAtIndex(Value obj, int argCount, Value *args) {
  (void)argCount;
  ObjArray *array = AS_ARRAY(obj);
  int idx = (int)AS_NUMBER(args[0]);
  Value value = NIL_VAL;
  if (idx >= 0 && idx < array->arr.count) {
    getValueArray(&array->arr, idx, &value);
  }
  return value;
}

static Value setArrayAtIndex(Value obj, int argCount, Value *args) {
  (void)argCount;
  ObjArray *array = AS_ARRAY(obj);
  int idx = (int)AS_NUMBER(args[0]);
  if (idx >= 0 && idx < array->arr.count) {
    setValueArray(&array->arr, idx, &args[1]);
  }
  return args[1];
}

// --------------------------------------------------------------

ObjPrototype *objArrayPrototype = NULL;

void initArrayModule() {
  objArrayPrototype = newPrototype(objPrototype);
  // init array prototype
  ObjString *length_str = copyString("length", 6);
  length_str->obj.flags = GC_DONT_COLLECT;
  tableSet(&objArrayPrototype->propsNative, length_str,
           OBJ_VAL((Obj*)newNativeProp(lenArray, NULL, length_str)));

  ObjString *set_index_str = copyString("__setitem__", 11);
  set_index_str->obj.flags = GC_DONT_COLLECT;
  tableSet(&objArrayPrototype->methodsNative, set_index_str,
           OBJ_VAL((Obj*)newNativeMethod(setArrayAtIndex, set_index_str, 2)));


  ObjString *get_index_str = copyString("__getitem__", 11);
  get_index_str->obj.flags = GC_DONT_COLLECT;
  tableSet(&objArrayPrototype->methodsNative, get_index_str,
           OBJ_VAL((Obj*)newNativeMethod(getArrayAtIndex, get_index_str, 1)));



  ObjString *push_str = copyString("push", 4);
  push_str->obj.flags = GC_DONT_COLLECT;
  tableSet(&objArrayPrototype->methodsNative, push_str,
           OBJ_VAL((Obj*)newNativeMethod(pushArray, push_str, 1)));


  ObjString *pop_str = copyString("pop", 3);
  pop_str->obj.flags = GC_DONT_COLLECT;
  tableSet(&objArrayPrototype->methodsNative, pop_str,
           OBJ_VAL((Obj*)newNativeMethod(popArray, pop_str, 0)));


}

void freeArrayModule() {
  // Should get freed automatically when freeObjectModule runs
  objArrayPrototype = NULL;
}

ObjArray *newArray() {
  ObjArray *array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
  array->obj.prototype = objArrayPrototype;
  initValueArray(&array->arr);
  return array;
}

Value lenArray(Value array) {
  ObjArray *obj = AS_ARRAY(array);
  return NUMBER_VAL(obj->arr.count);
}

Value popArray(Value array, int argCount, Value *args) {
  (void)argCount;(void)args;
  Value ret;
  if (popValueArray(&AS_ARRAY(array)->arr, &ret))
    return ret;
  return NIL_VAL;
}

Value pushArray(Value array, int argCount, Value *args) {
  (void)argCount;
  pushValueArray(&AS_ARRAY(array)->arr, args[0]);
  return args[0];
}

Value toStringArray(Value array) {
  char *buf;
  int len = arrayToString(&buf, AS_ARRAY(array));
  ObjString *ret = copyString(buf, len);
  FREE_ARRAY(char, buf, len);
  return OBJ_VAL(OBJ_CAST(ret));
}

