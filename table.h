#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

typedef struct Entry {
  Value key;
  Value value;
} Entry;

typedef struct Table {
  int count;
  int capacityMask;
  Entry *entries;
} Table;

void initTable(Table *table);
void freeTable(Table *table);

// fills given Value with found value, if any
bool tableGet(Table *table, Value key, Value *value);
bool tableSet(Table *table, Value key, Value value);
bool tableDelete(Table *table, Value key);
void tableAddAll(Table *from, Table *to);

ObjString *tableFindString(Table* table, const char* chars, int length,
                           uint32_t hash);

void tableRemoveWhite(Table *table);
void grayTable(Table *table);

#endif
