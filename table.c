#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "debug.h"

#define TABLE_MAX_LOAD 0.75

// sentinel value for NULL key
Value TBL_EMPTY_KEY = {
    .type = VAL_T_UNDEF,
    .as = { .number = (double)0.00 }
};

void initTable(Table *table) {
    table->count = 0;
    // There are, at most, capacityMask+1 existing entry pointers in the table
    table->capacityMask = -1;
    table->entries = NULL;
}

void freeTable(Table *table) {
    FREE_ARRAY(Value, table->entries, table->capacityMask + 1);
    initTable(table);
}

static uint32_t findEntry(Entry *entries, int capacityMask, Value key) {
    uint32_t index = valHash(key) & capacityMask;

    // We don't worry about an infinite loop here because resize() ensures
    // there are empty slots in the array.
    for (;;) {
        Entry *entry = &entries[index];

        if ((entry->key.type == VAL_T_UNDEF) || (valEqual(entry->key, key))) {
            return index;
        }

        index = (index + 1) & capacityMask;
    }
}

bool tableGet(Table *table, Value key, Value *value) {
    // If the table is empty, we definitely won't find it.
    if (table->entries == NULL) return false;

    uint32_t index = findEntry(table->entries, table->capacityMask, key);
    Entry *entry = &table->entries[index];
    if (entry->key.type == VAL_T_UNDEF) return false;
    *value = entry->value;
    return true;
}

static void resize(Table *table, int capacityMask) {
    /*fprintf(stderr, "Resize\n");*/
    Entry *entries = ALLOCATE(Entry, capacityMask + 1);
    for (int i = 0; i <= capacityMask; i++) {
        entries[i].key = TBL_EMPTY_KEY;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i <= table->capacityMask; i++) {
        Entry *entry = &table->entries[i];
        if (entry->key.type == VAL_T_UNDEF) continue;

        uint32_t index = findEntry(entries, capacityMask, entry->key);
        Entry *dest = &entries[index];
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    /*fprintf(stderr, "/Resize freearray\n");*/
    FREE_ARRAY(Entry, table->entries, table->capacityMask + 1);
    /*fprintf(stderr, "/Resize freearray\n");*/
    table->entries = entries;
    table->capacityMask = capacityMask;
    /*fprintf(stderr, "/Resize\n");*/
}

bool tableSet(Table *table, Value key, Value value) {
    /*fprintf(stderr, "tableSet\n");*/
    if (table->count + 1 > (table->capacityMask + 1) * TABLE_MAX_LOAD) {
        // Figure out the new table size.
        int capacityMask = GROW_CAPACITY(table->capacityMask + 1) - 1;
        resize(table, capacityMask);
    }

    /*fprintf(stderr, "findEntry\n");*/
    uint32_t index = findEntry(table->entries, table->capacityMask, key);
    /*fprintf(stderr, "/findEntry\n");*/
    Entry *entry = &table->entries[index];
    bool isNewKey = entry->key.type == VAL_T_UNDEF;
    entry->key = key;
    entry->value = value;

    if (isNewKey) table->count++;
    /*fprintf(stderr, "/tableSet\n");*/
    return isNewKey;
}

bool tableDelete(Table* table, Value key) {
    if (table->count == 0) return false;

    uint32_t index = findEntry(table->entries, table->capacityMask, key);
    Entry *entry = &table->entries[index];
    if (entry->key.type == VAL_T_UNDEF) return false;

    // Remove the entry.
    entry->key = TBL_EMPTY_KEY;
    entry->value = NIL_VAL;
    table->count--;

    // Later entries may have been pushed past this one and may need to be pushed
    // up to fill the hole. The simplest way to handle that is to just re-add
    // them all until we hit an empty entry.
    for (;;) {
        index = (index + 1) & table->capacityMask;
        entry = &table->entries[index];

        if (entry->key.type == VAL_T_UNDEF) break;

        Value tempKey = entry->key;
        Value tempValue = entry->value;
        entry->key = TBL_EMPTY_KEY;
        entry->value = NIL_VAL;
        table->count--;

        tableSet(table, tempKey, tempValue);
    }

    return true;
}

void tableEachEntry(Table *table, TableEntryCb cb) {
    int numEntrySlots = table->capacityMask+1;
    if (table->count == 0) return;
    for (int i = 0; i < numEntrySlots; i++) {
        Entry e = table->entries[i];
        if (e.key.type != VAL_T_UNDEF) {
            cb(&e);
        }
    }
}

void tableAddAll(Table *from, Table *to) {
    if (from->entries == NULL) return;
    for (int i = 0; i <= from->capacityMask; i++) {
        Entry *entry = &from->entries[i];
        if (entry->key.type != VAL_T_UNDEF) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

ObjString *tableFindString(Table *table, const char* chars, int length,
        uint32_t hash) {
    // If the table is empty, we definitely won't find it.
    if (table->entries == NULL) return NULL;

    uint32_t index = hash & table->capacityMask;

    for (;;) {
        Entry *entry = &table->entries[index];

        if (entry->key.type == VAL_T_UNDEF) return NULL;
        if (IS_STRING(entry->key)) {
            ObjString *stringKey = AS_STRING(entry->key);
            if (stringKey->length == length &&
                    memcmp(stringKey->chars, chars, length) == 0) {
                // We found it.
                return (ObjString*)AS_OBJ(entry->key);
            }
        }

        // Try the next slot.
        index = (index + 1) & table->capacityMask;
    }

    return NULL;
}

// remove unmarked object keys from table
void tableRemoveWhite(Table *table) {
    if (table->count == 0) return;
    for (int i = 0; i <= table->capacityMask; i++) {
        Entry *entry = &table->entries[i];
        if (entry->key.type == VAL_T_UNDEF) {
            continue;
        }
        if (IS_OBJ(entry->key) && !AS_OBJ(entry->key)->isDark) {
            tableDelete(table, entry->key);
        }
    }
}

void grayTable(Table *table) {
    if (table->count == 0) return;
    for (int i = 0; i <= table->capacityMask; i++) {
        ASSERT(table->entries);
        Entry *entry = &table->entries[i];
        if (!entry || (entry->key.type == VAL_T_UNDEF)) continue;
        ASSERT(entry);
        grayValue(entry->key);
        grayValue(entry->value);
    }
}

void blackenTable(Table *table) {
    if (table->count == 0) return;
    for (int i = 0; i <= table->capacityMask; i++) {
        ASSERT(table->entries);
        Entry *entry = &table->entries[i];
        if (!entry || (entry->key.type == VAL_T_UNDEF)) continue;
        ASSERT(entry);
        if (IS_OBJ(entry->key)) {
            blackenObject(AS_OBJ(entry->key));
        }
        if (IS_OBJ(entry->value)) {
            blackenObject(AS_OBJ(entry->value));
        }
    }
}
