#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "debug.h"

#define TABLE_MAX_LOAD 0.75

/**
 * TODO: if two different values hash to the same number, the first is
 * overridden. This is an implementation issue, `table->entries` should be a 2
 * dimensional array, backed by a dynamic array for each hash bucket.
 */

// sentinel value for NULL key
#ifdef NAN_TAGGING
Value TBL_EMPTY_KEY = UNDEF_VAL;
Entry TBL_EMPTY_ENTRY = {
    .key = UNDEF_VAL,
    .value = NIL_VAL
};
#else
Value TBL_EMPTY_KEY = {
    .type = VAL_T_UNDEF,
    .as = { .number = (double)0.00 }
};

Entry TBL_EMPTY_ENTRY = {
    .key = {
        .type = VAL_T_UNDEF,
        .as = { .number = (double)0.00 }
    },
    .value = NIL_VAL
};
#endif

void initTable(Table *table) {
    table->count = 0;
    // There are, at most, capacityMask+1 existing entry pointers in the table
    table->capacityMask = -1;
    table->entries = NULL;
}

void initTableWithCapa(Table *table, size_t capa) {
    if (capa == 0) {
        initTable(table);
        return;
    }
    table->count = 0;
    // There are, at most, capacityMask+1 existing entry pointers in the table
    table->capacityMask = (int)(capa-1);
    Entry *entries = ALLOCATE(Entry, capa);
    for (size_t i = 0 ; i < capa; i++) {
        memcpy(entries+i, &TBL_EMPTY_ENTRY, sizeof(Entry));
    }
    table->entries = entries;
}

size_t tableCapacity(Table *table) {
    return (size_t)(table->capacityMask+1);
}

void freeTable(Table *table) {
    FREE_ARRAY(Entry, table->entries, table->capacityMask + 1);
    initTable(table);
}

static uint32_t findEntry(Entry *entries, int capacityMask, Value key) {
    // NOTE: valHash() can call method `hashKey()` if key is an instance
    uint32_t index = valHash(key) & capacityMask;

    // We don't worry about an infinite loop here because resize() ensures
    // there are empty slots in the array.
    for (;;) {
        Entry *entry = &entries[index];

        // NOTE: valEqual() can call `opEquals()` if entry key is an instance
        if ((IS_UNDEF(entry->key)) || (valEqual(entry->key, key))) {
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
    if (IS_UNDEF(entry->key)) return false;
    *value = entry->value;
    return true;
}

static void resize(Table *table, int capacityMask) {
    /*fprintf(stderr, "Resize\n");*/
    Entry *entries = ALLOCATE(Entry, capacityMask + 1);
    for (int i = 0; i <= capacityMask; i++) {
        memcpy(entries+i, &TBL_EMPTY_ENTRY, sizeof(Entry));
    }

    table->count = 0;
    for (int i = 0; i <= table->capacityMask; i++) {
        Entry *entry = &table->entries[i];
        if (IS_UNDEF(entry->key)) continue;

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
    bool isNewKey = IS_UNDEF(entry->key);
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
    if (IS_UNDEF(entry->key)) return false;

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

        if (IS_UNDEF(entry->key)) break;

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
        if (!IS_UNDEF(e.key)) {
            cb(&e);
        }
    }
}

void tableAddAll(Table *from, Table *to) {
    if (from->entries == NULL) return;
    for (int i = 0; i <= from->capacityMask; i++) {
        Entry *entry = &from->entries[i];
        if (!IS_UNDEF(entry->key)) {
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

        if (UNLIKELY(IS_UNDEF(entry->key))) return NULL;
        if (IS_STRING(entry->key)) {
            ObjString *stringKey = AS_STRING(entry->key);
            if (stringKey->hash && stringKey->hash == hash) {
                return stringKey;
            }
            if (stringKey->length == length &&
                    memcmp(stringKey->chars, chars, length) == 0) {
                // We found it.
                return stringKey;
            }
        }

        // Try the next slot.
        index = (index + 1) & table->capacityMask;
    }

    return NULL;
}

Entry tableNthEntry(Table *table, int n, int *entryIndex) {
    Entry e = TBL_EMPTY_ENTRY; int entryIdx = 0;
    int validEntryIdx = 0;
    TABLE_FOREACH(table, e, entryIdx, {
        if (n == validEntryIdx) {
            *entryIndex = entryIdx;
            return e;
        }
        validEntryIdx++;
    })
    *entryIndex = -1;
    return e; // trashed data in this case, caller should always check entryIndex out value
}

// remove unmarked object keys from table
void tableRemoveWhite(Table *table) {
    if (table->count == 0) return;
    for (int i = 0; i <= table->capacityMask; i++) {
        Entry *entry = &table->entries[i];
        if (IS_UNDEF(entry->key)) {
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
        if (!entry || (IS_UNDEF(entry->key))) continue;
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
        if (!entry || (IS_UNDEF(entry->key))) continue;
        ASSERT(entry);
        if (IS_OBJ(entry->key)) {
            blackenObject(AS_OBJ(entry->key));
        }
        if (IS_OBJ(entry->value)) {
            blackenObject(AS_OBJ(entry->value));
        }
    }
}
