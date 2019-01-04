#ifndef clox_memory_h
#define clox_memory_h

#include <stdio.h>
#include "common.h"
#include "object.h"
#include "value.h"

#define GC_GEN_YOUNG_MAX 2
#define GC_GEN_MAX 5

// NOTE: zeroes the memory
#define ALLOCATE(type, count)\
    /*fprintf(stderr, "Allocating %d %s\n", (int)count, xstr(type)) > 0 ? \*/\
    (type*)reallocate(NULL, 0, sizeof(type) * (count)) /*: NULL */

#define FREE(type, pointer) \
    reallocate(pointer, sizeof(type), 0)

#define FREE_SIZE(oldSize, pointer) \
    reallocate(pointer, oldSize, 0)

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(previous, type, oldCount, count) \
    (type*)reallocate(previous, sizeof(type) * (oldCount), \
        sizeof(type) * (count))


#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

#define GC_HEAP_GROW_FACTOR 2

#define xfree free

struct sGCProfile {
    struct timeval totalGCTime;
    unsigned long totalRuns;
};

extern struct sGCProfile GCProf;
extern int activeFinalizers;

struct sGCStats {
    // all in bytes
    size_t totalAllocated;
    size_t heapSize;
    size_t heapUsed;
    size_t heapUsedWaste;
    unsigned long demographics[OBJ_T_LAST];
    unsigned long generations[GC_GEN_MAX+1];
};

extern struct sGCStats GCStats;

void *reallocate(void *previous, size_t oldSize, size_t newSize);

void grayObject(Obj *obj); // non-recursively mark object as live
void grayValue(Value val); // non-recursively mark object in value as live
void collectGarbage(void); // do 1 mark+sweep
void freeObject(Obj *obj);
void blackenObject(Obj *obj); // recursively mark object's references
void freeObjects(void); // free all vm.objects. Used at end of VM lifecycle

void GCPromote(Obj *obj, unsigned short gen);

bool turnGCOff(void);
bool turnGCOn(void);
void setGCOnOff(bool turnOn);
void hideFromGC(Obj *obj);
void unhideFromGC(Obj *obj);

void printGCProfile(void);

void addHeap(void);
Obj *getNewObject(ObjType type, size_t sz);

#endif
