#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

/* Value table, maps values to values */

typedef struct Entry {
  Value key;
  Value value;
} Entry;

typedef struct Table {
  int count;
  int capacityMask;
  Entry *entries;
} Table;

typedef void (*TableEntryCb)(Entry *e);

void initTable(Table *table);
void initTableWithCapa(Table *table, size_t capa);
void freeTable(Table *table); // free internal table structures, not table itself


// fills given Value with found value, if any
bool tableGet(Table *table, Value key, Value *value);
bool tableSet(Table *table, Value key, Value value);
bool tableDelete(Table *table, Value key);
void tableAddAll(Table *from, Table *to);
void tableEachEntry(Table *table, TableEntryCb func);
Entry tableNthEntry(Table *table, int n, int *entryIdx);

size_t tableCapacity(Table *table);

#ifdef NAN_TAGGING
#define TABLE_FOREACH(tbl, entry, idx, exec)\
  if ((tbl)->count > 0) {\
    for ((idx) = 0;\
         (((idx) < (tbl)->capacityMask+1) &&\
         (entry = (tbl)->entries[idx]).key != QNAN); (idx)++) {\
        if (entry.key == UNDEF_VAL) { continue; } else {\
            exec\
        }\
    }\
  }
#define TABLE_FOREACH_IDX(tbl, entry, idx, exec)\
  if ((tbl)->count > 0) {\
    for (;\
         (((idx) < (tbl)->capacityMask+1) &&\
         (entry = (tbl)->entries[idx]).key != QNAN); (idx)++) {\
        if (entry.key == UNDEF_VAL) { continue; } else {\
            exec\
        }\
    }\
  }
#else
// NOTE: condition with value '555' uses this value just so that the rhs of
// the expression results in a valid test expression, instead of just the
// assignment itself. This test always returns true.
#define TABLE_FOREACH(tbl, entry, idx, exec)\
  if ((tbl)->count > 0) {\
    for ((idx) = 0;\
         (((idx) < (tbl)->capacityMask+1) &&\
         (entry = (tbl)->entries[idx]).key.type != 555); (idx)++) {\
        if (entry.key.type == VAL_T_UNDEF) { continue; } else {\
            exec\
        }\
    }\
  }
#define TABLE_FOREACH_IDX(tbl, entry, idx, exec)\
  if ((tbl)->count > 0) {\
    for (;\
         (((idx) < (tbl)->capacityMask+1) &&\
         (entry = (tbl)->entries[idx]).key.type != 555); (idx)++) {\
        if (entry.key.type == VAL_T_UNDEF) { continue; } else {\
            exec\
        }\
    }\
  }
#endif

ObjString *tableFindString(Table* table, const char* chars, int length,
                           uint32_t hash);

void tableRemoveWhite(Table *table);
void grayTable(Table *table);
void blackenTable(Table *table);

#endif
