#ifndef clox_memory_h
#define clox_memory_h

#include <stdio.h>
#include "common.h"
#include "object.h"
#include "value.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GC_GEN_MIN 0
#define GC_GEN_YOUNG_MAX 1
#define GC_GEN_OLD_MIN 2
#define GC_GEN_OLD 3
#define GC_GEN_MAX 4

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
#define  xmalloc malloc
#define  xcalloc calloc

struct sGCProfile {
    struct timeval totalGCYoungTime;
    struct timeval totalGCFullTime;
    unsigned long runsYoung;
    unsigned long runsFull;
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
void nil_mem(Value *mem, size_t num);

void grayObject(Obj *obj); // non-recursively mark object as live
void grayValue(Value val); // non-recursively mark object in value as live
void collectGarbage(void); // do 1 mark+sweep
void collectYoungGarbage(void); // collect young objects
void freeObject(Obj *obj);
void blackenObject(Obj *obj); // recursively mark object's references
void freeObjects(void); // free all vm.objects. Used at end of VM lifecycle

#define GC_PROMOTE(obj, gen) GCPromote((Obj*)obj, gen)
#define GC_PROMOTE_ONCE(obj) GCPromoteOnce((Obj*)obj)
#define GC_OLD(obj) GCPromote((Obj*)obj, GC_GEN_MAX)
void GCPromote(Obj *obj, unsigned short gen);
void GCPromoteOnce(Obj *obj);
void pushRememberSet(Obj *obj);

#define IS_OLD_VAL(value) (AS_OBJ(value)->GCGen > GC_GEN_MIN)
#define IS_YOUNG_VAL(value) (AS_OBJ(value)->GCGen == GC_GEN_MIN)
#define IS_OLD_OBJ(obj) ((obj)->GCGen > GC_GEN_MIN)
#define IS_YOUNG_OBJ(obj) ((obj)->GCGen == GC_GEN_MIN)
static inline void objWrite(Value owner, Value pointed) {
    bool hasFinalizer = false;
    if ((IS_OBJ(pointed) && IS_YOUNG_VAL(pointed)) && (IS_OLD_VAL(owner) || (hasFinalizer = OBJ_HAS_FINALIZER(AS_OBJ(owner))))) {
        if (hasFinalizer) {
            OBJ_SET_HAS_FINALIZER(AS_OBJ(pointed));
        }
        pushRememberSet(AS_OBJ(pointed));
    }
}
#define OBJ_WRITE(owner, pointed) objWrite(owner, pointed)

extern bool inYoungGC;
extern bool inFullGC;
extern bool inFinalFree;

bool turnGCOff(void);
bool turnGCOn(void);
void setGCOnOff(bool turnOn);
void hideFromGC(Obj *obj);
void unhideFromGC(Obj *obj);

void printGCProfile(void);

void addHeap(void);
Obj *getNewObject(ObjType type, size_t sz, int flags);

#ifdef __cplusplus
}
#endif

#endif
