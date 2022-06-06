#ifndef CLOX_TABLE_H
#define CLOX_TABLE_H

#include "common.h"
#include "value.h"
#include "object.h"

typedef struct {
  ObjString *key;
  Value value;
} Entry;


typedef struct {
  int count,
      capacity;
      Entry *entries;
} Table;

void initTable(Table *table);
void freeTable(Table *table);
bool tableGet(Table *table, ObjString *key, Value *value);
bool tableSet(Table *table, ObjString *key, Value value);
bool tableDelete(Table *table, ObjString *key);
void tableAddAll(Table *from, Table *to);
ObjString *tableFindString(Table *table, const char *chars,
                           int length, uint32_t hash);
void tableRemoveWhite(Table *table, ObjFlags flags);
void markTable(Table *table, ObjFlags flags);

// reciever responsable for calling freeArray
ValueArray tableKeys(const Table *table);


#endif // CLOX_TABLE_H
