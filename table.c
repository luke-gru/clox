#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "debug.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
    table->count = 0;
    table->capacityMask = -1;
    table->entries = NULL;
}

void freeTable(Table* table) {
    FREE_ARRAY(Value, table->entries, table->capacityMask + 1);
    initTable(table);
}

static uint32_t findEntry(Entry* entries, int capacityMask, ObjString* key) {
    uint32_t index = key->hash & capacityMask;

    // We don't worry about an infinite loop here because resize() ensures
    // there are empty slots in the array.
    for (;;) {
        Entry* entry = &entries[index];

        if (entry->key == NULL || entry->key == key) return index;

        index = (index + 1) & capacityMask;
    }
}

bool tableGet(Table* table, ObjString* key, Value* value) {
    // If the table is empty, we definitely won't find it.
    if (table->entries == NULL) return false;

    uint32_t index = findEntry(table->entries, table->capacityMask, key);
    Entry* entry = &table->entries[index];
    if (entry->key == NULL) return false;
    *value = entry->value;
    return true;
}

static void resize(Table* table, int capacityMask) {
    Entry* entries = ALLOCATE(Entry, capacityMask + 1);
    for (int i = 0; i <= capacityMask; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i <= table->capacityMask; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key == NULL) continue;

        uint32_t index = findEntry(entries, capacityMask, entry->key);
        Entry* dest = &entries[index];
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(Value, table->entries, table->capacityMask + 1);
    table->entries = entries;
    table->capacityMask = capacityMask;
}

bool tableSet(Table* table, ObjString* key, Value value) {
    if (table->count + 1 > (table->capacityMask + 1) * TABLE_MAX_LOAD) {
        // Figure out the new table size.
        int capacityMask = GROW_CAPACITY(table->capacityMask + 1) - 1;
        resize(table, capacityMask);
    }

    uint32_t index = findEntry(table->entries, table->capacityMask, key);
    Entry* entry = &table->entries[index];
    bool isNewKey = entry->key == NULL;
    entry->key = key;
    entry->value = value;

    if (isNewKey) table->count++;
    return isNewKey;
}

bool tableDelete(Table* table, ObjString* key) {
    if (table->count == 0) return false;

    uint32_t index = findEntry(table->entries, table->capacityMask, key);
    Entry* entry = &table->entries[index];
    if (entry->key == NULL) return false;

    // Remove the entry.
    entry->key = NULL;
    entry->value = NIL_VAL;
    table->count--;

    // Later entries may have been pushed past this one and may need to be pushed
    // up to fill the hole. The simplest way to handle that is to just re-add
    // them all until we hit an empty entry.
    for (;;) {
        index = (index + 1) & table->capacityMask;
        entry = &table->entries[index];

        if (entry->key == NULL) break;

        ObjString* tempKey = entry->key;
        Value tempValue = entry->value;
        entry->key = NULL;
        entry->value = NIL_VAL;
        table->count--;

        tableSet(table, tempKey, tempValue);
    }

    return true;
}

void tableAddAll(Table* from, Table* to) {
    if (from->entries == NULL) return;
    for (int i = 0; i <= from->capacityMask; i++) {
        Entry* entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

ObjString* tableFindString(Table* table, const char* chars, int length,
        uint32_t hash) {
    // If the table is empty, we definitely won't find it.
    if (table->entries == NULL) return NULL;

    uint32_t index = hash & table->capacityMask;

    for (;;) {
        Entry* entry = &table->entries[index];

        if (entry->key == NULL) return NULL;
        if (entry->key->length == length &&
                memcmp(entry->key->chars, chars, length) == 0) {
            // We found it.
            return entry->key;
        }

        // Try the next slot.
        index = (index + 1) & table->capacityMask;
    }

    return NULL;
}

void tableRemoveWhite(Table* table) {
    if (table->count == 0) return;
    for (int i = 0; i <= table->capacityMask; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->object.isDark) {
            tableDelete(table, entry->key);
        }
    }
}

void grayTable(Table *table) {
    if (table->count == 0) return;
    for (int i = 0; i <= table->capacityMask; i++) {
        ASSERT(table->entries);
        Entry *entry = &table->entries[i];
        if (!entry || !entry->key) continue;
        ASSERT(entry);
        grayObject((Obj*)entry->key);
        grayValue(entry->value);
    }
}
