#ifndef clox_memory_h
#define clox_memory_h

#include "object.h"
#include "value.h"

// NOTE: zeroes the memory
#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

#define FREE(type, pointer) \
    reallocate(pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(previous, type, oldCount, count) \
    (type*)reallocate(previous, sizeof(type) * (oldCount), \
        sizeof(type) * (count))


#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

#define GC_HEAP_GROW_FACTOR 2

void *reallocate(void *previous, size_t oldSize, size_t newSize);

void grayObject(Obj *obj); // non-recursively mark object as live
void grayValue(Value val); // non-recursively mark object in value as live
void collectGarbage(void); // begin GC
void freeObject(Obj *obj);
void blackenObject(Obj *obj);
void freeObjects(void); // free all objects, at end of VM lifecycle

void turnGCOff(void);
void turnGCOn(void);
void hideFromGC(Obj *obj);
void unhideFromGC(Obj *obj);

#endif
