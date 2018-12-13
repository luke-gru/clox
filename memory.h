#ifndef clox_memory_h
#define clox_memory_h

#include <stdio.h>
#include "object.h"
#include "value.h"

#define xstr(a) #a

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

void *reallocate(void *previous, size_t oldSize, size_t newSize);

void grayObject(Obj *obj); // non-recursively mark object as live
void grayValue(Value val); // non-recursively mark object in value as live
void collectGarbage(void); // do 1 mark+sweep
void freeObject(Obj *obj, bool doUnlink);
void blackenObject(Obj *obj); // recursively mark object's references
void freeObjects(void); // free all vm.objects. Used at end of VM lifecycle

bool turnGCOff(void);
bool turnGCOn(void);
void setGCOnOff(bool turnOn);
void hideFromGC(Obj *obj);
void unhideFromGC(Obj *obj);

#endif
