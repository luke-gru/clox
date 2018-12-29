#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include "common.h"
#include "memory.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"
#include "value.h"
#include "options.h"

#ifdef NDEBUG
#define GC_TRACE_MARK(lvl, obj) (void)0
#define GC_TRACE_FREE(lvl, obj) (void)0
#define GC_TRACE_DEBUG(lvl, ...) (void)0
#define TRACE_GC_FUNC_START(lvl, func) (void)0
#define TRACE_GC_FUNC_END(lvl, func)   (void)0
#else
#define GC_TRACE_MARK(lvl, obj) gc_trace_mark(lvl, obj)
#define GC_TRACE_FREE(lvl, obj) gc_trace_free(lvl, obj)
#define GC_TRACE_DEBUG(lvl, ...) gc_trace_debug(lvl, __VA_ARGS__)
#define TRACE_GC_FUNC_START(lvl, func) trace_gc_func_start(lvl, func);
#define TRACE_GC_FUNC_END(lvl, func) trace_gc_func_end(lvl, func);
#endif

#define HEAPLIST_INCREMENT 10
#define FREE_MIN 500
#define HEAP_SLOTS 10000
static ObjAny **heapList;
static int heapListSize = 0;
static ObjAny *freeList;
static int heapsUsed = 0;

static bool inGC = false;
static bool GCOn = true;
static bool dontGC = false;

struct sGCProfile GCProf = {
    .totalGCTime = {
        .tv_sec = 0,
        .tv_usec = 0,
    },
    .totalRuns = 0
};

static void startGCRunProfileTimer(struct timeval *timeStart) {
    gettimeofday(timeStart, NULL);
}
static void stopGCRunProfileTimer(struct timeval *timeStart) {
    struct timeval timeEnd;
    gettimeofday(&timeEnd, NULL);
    struct timeval tdiff = { .tv_sec = 0, .tv_usec = 0 };
    timersub(&timeEnd, timeStart, &tdiff);
    struct timeval tres;
    timeradd(&GCProf.totalGCTime, &tdiff, &tres);
    GCProf.totalGCTime = tres; // copy
}

struct sGCStats GCStats;
int activeFinalizers = 0;

static void printGenerationInfo() {
    fprintf(stderr, "Generation info:\n");
    for (int i = 0; i <= GC_GEN_MAX; i++) {
        fprintf(stderr, "Gen %d: %lu\n", i, GCStats.generations[i]);
    }
}

static void printObjTypeSizes() {
    int type = OBJ_T_NONE+1;
    while (type < OBJ_T_LAST) {
        fprintf(stderr, "%s size: %ld\n", objTypeName(type), sizeofObjType(type));
        type++;
    }
}

static void printGCDemographics() {
    for (int i = OBJ_T_NONE+1; i < OBJ_T_LAST; i++) {
        fprintf(stderr, "# %s: %ld\n", objTypeName(i), GCStats.demographics[i]);
    }
}

static void printGCStats() {
    fprintf(stderr, "GC Stats\n");
    if (GET_OPTION(traceGCLvl > 2)) {
        printObjTypeSizes();
    }
    fprintf(stderr, "ObjAny size: %ld b\n", sizeof(ObjAny));
    fprintf(stderr, "Total allocated: %ld KB\n", GCStats.totalAllocated/1024);
    fprintf(stderr, "Heap size: %ld KB\n", GCStats.heapSize/1024);
    fprintf(stderr, "Heap used: %ld KB\n", GCStats.heapUsed/1024);
    fprintf(stderr, "Heap used waste: %ld KB\n", GCStats.heapUsedWaste/1024);
    fprintf(stderr, "# heaps used: %d\n", heapsUsed);
    fprintf(stderr, "# objects: %ld\n", GCStats.heapUsed/sizeof(ObjAny));
    if (GET_OPTION(traceGCLvl > 3)) {
        printGCDemographics();
    }
}


void printGCProfile() {
    fprintf(stderr, "Total runs: %lu\n", GCProf.totalRuns);
    time_t secs = GCProf.totalGCTime.tv_sec;
    suseconds_t msecs = GCProf.totalGCTime.tv_usec;
    suseconds_t millis = (msecs / 1000);
    while (millis > 1000) {
        secs += 1;
        millis = millis / 1000;
    }
    fprintf(stderr, "Total GC time: %ld secs, %ld ms\n",
            secs, (long)millis);
}

void GCPromote(Obj *obj, unsigned short gen) {
    if (gen > GC_GEN_MAX) gen = GC_GEN_MAX;
    unsigned short oldGen = obj->GCGen;
    GCStats.generations[oldGen]--;
    GCStats.generations[gen]++;
    obj->GCGen = gen;
}

static void gc_trace_mark(int lvl, Obj *obj) {
    if (GET_OPTION(traceGCLvl) < lvl) return;
    fprintf(stderr, "[GC]: marking %s object at %p", typeOfObj(obj), obj);
    if (obj->type != OBJ_T_UPVALUE && obj->type != OBJ_T_INTERNAL) {
        fprintf(stderr, ", value => ");
        bool oldGC = inGC;
        inGC = false;
        printValue(stderr, OBJ_VAL(obj), false, -1); // can allocate objects, must be `inGC`
        inGC = oldGC;
    }
    fprintf(stderr, "\n");
}

static void gc_trace_free(int lvl, Obj *obj) {
    if (GET_OPTION(traceGCLvl) < lvl) return;
    fprintf(stderr, "[GC]: freeing object at %p, ", obj);
    if (obj->type == OBJ_T_UPVALUE) {
        fprintf(stderr, "type => upvalue");
    } else {
        fprintf(stderr, "type => %s, value => ", typeOfObj(obj));
        bool oldGC = inGC;
        inGC = false;
        printValue(stderr, OBJ_VAL(obj), false, -1); // can allocate objects, must be `inGC`
        if (obj->type == OBJ_T_INSTANCE) {
            const char *className = "(anon)";
            if (((ObjInstance*) obj)->klass->name) {
                className = ((ObjInstance*) obj)->klass->name->chars;
            }
            fprintf(stderr, ", class => %s", className);
        }
        inGC = oldGC;
    }
    fprintf(stderr, "\n");
}

static void gc_trace_debug(int lvl, const char *fmt, ...) {
    if (GET_OPTION(traceGCLvl) < lvl) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[GC]: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static inline void trace_gc_func_start(int lvl, const char *funcName) {
    if (GET_OPTION(traceGCLvl) < lvl) return;
    fprintf(stderr, "[GC]: <%s>\n", funcName);
}
static inline void trace_gc_func_end(int lvl, const char *funcName) {
    if (GET_OPTION(traceGCLvl) < lvl) return;
    fprintf(stderr, "[GC]: </%s>\n", funcName);
}

void addHeap() {
    ObjAny *p, *pend;

    if (heapsUsed == heapListSize) {
        /* Realloc heaps */
        heapListSize += HEAPLIST_INCREMENT;
        size_t newHeapListSz = heapListSize*sizeof(void*);
        heapList = (heapsUsed > 0) ?
            (ObjAny**)realloc(heapList, newHeapListSz) :
            (ObjAny**)malloc(newHeapListSz);
        if (heapList == 0) {
            fprintf(stderr, "can't alloc new heap list\n");
            _exit(1);
        }
        memset(heapList, 0, newHeapListSz);
        GCStats.totalAllocated += newHeapListSz;
    }

    size_t heapSz = sizeof(ObjAny)*HEAP_SLOTS;
    p = heapList[heapsUsed++] = (ObjAny*)malloc(heapSz);
    if (p == 0) {
        fprintf(stderr, "addHeap: can't alloc new heap\n");
        _exit(1);
    }
    GCStats.totalAllocated += heapSz;
    GCStats.heapSize += heapSz;
    pend = p + HEAP_SLOTS;

    while (p < pend) { // zero out new objects
        Obj *obj = (Obj*)p;
        obj->type = OBJ_T_NONE;
        obj->nextFree = freeList;
        freeList = (ObjAny*)obj;
        p++;
    } // freeList points to last free entry in list, linked backwards
}

// TODO: we shouldn't free all heaps right away, we should leave one
// empty heap and mark it as empty, then we don't need to iterate over
// it during GC, and we return it on next call to addHeap().
void freeHeap(ObjAny *heap) {
    int i = 0;
    ObjAny *curHeap = NULL;
    int heapIdx = -1;
    for (i = 0; i < heapListSize; i++) {
        curHeap = heapList[i];
        if (curHeap && curHeap == heap) {
            heapIdx = i;
        }
    }
    ASSERT(heapIdx != -1);
    memmove(heapList+heapIdx, heapList+heapIdx+1, heapListSize-heapIdx-1);
    xfree(heap);
    heapsUsed--;
    GCStats.totalAllocated -= (sizeof(ObjAny)*HEAP_SLOTS);
    GCStats.heapSize -= (sizeof(ObjAny)*HEAP_SLOTS);
}

Obj *getNewObject(ObjType type, size_t sz) {
    (void)type; // TODO: keep track of how many of each object type in GCStats struct
    Obj *obj = NULL;

retry:
    if (freeList) {
        obj = (Obj*)freeList;
        freeList = obj->nextFree;
        GCStats.heapUsed += sizeof(ObjAny);
        GCStats.heapUsedWaste += (sizeof(ObjAny)-sz);
        GCStats.demographics[type]++;
        return obj;
    }
    if (!GCOn || dontGC) {
        addHeap();
    } else {
        collectGarbage(); // adds heap if needed at end of collection
    }
    goto retry;
}


// Main memory management function used by both ALLOCATE/FREE (see memory.h)
// NOTE: memory is NOT initialized (see man 3 realloc)
void *reallocate(void *previous, size_t oldSize, size_t newSize) {
    TRACE_GC_FUNC_START(10, "reallocate");
    if (newSize > 0) {
        ASSERT(!inGC); // if we're in GC phase we shouldn't allocate memory
    }

    if (newSize > oldSize) {
        GCStats.totalAllocated += (newSize - oldSize);
        GC_TRACE_DEBUG(12, "reallocate added %lu bytes", newSize-oldSize);
    } else {
        GCStats.totalAllocated -= (oldSize - newSize);
        GC_TRACE_DEBUG(12, "reallocate freed %lu bytes", oldSize-newSize);
    }

    if (newSize == 0) { // freeing
        GC_TRACE_DEBUG(10, "  freeing %p from realloc", previous);
        xfree(previous);
        TRACE_GC_FUNC_END(10, "reallocate");
        return NULL;
    }

    void *ret = realloc(previous, newSize);
    if (!ret) {
        GC_TRACE_DEBUG(1, "REALLOC FAILED, trying GC");
        collectGarbage(); // NOTE: GCOn could be false here if set by user
        // try again after potentially freeing memory
        void *ret = realloc(previous, newSize);
        if (!ret) {
            fprintf(stderr, "Out of memory!\n");
            _exit(1);
        }
    }
    if (newSize > 0) {
        GC_TRACE_DEBUG(10, "  allocated %p", ret);
    }
    TRACE_GC_FUNC_END(10, "reallocate");
    return ret;
}

static inline void INC_GEN(Obj *obj) {
    if (obj->GCGen == GC_GEN_MAX) return;
    obj->GCGen++;
    GCStats.generations[obj->GCGen-1]--;
    GCStats.generations[obj->GCGen]++;
}

static inline bool IS_YOUNG(Obj *obj) {
    return obj->GCGen <= GC_GEN_YOUNG_MAX;
}

void grayObject(Obj *obj) {
    TRACE_GC_FUNC_START(4, "grayObject");
    if (obj == NULL) {
        TRACE_GC_FUNC_END(4, "grayObject (null obj found)");
        return;
    }
    if (obj->isDark) {
        TRACE_GC_FUNC_END(4, "grayObject (already dark)");
        return;
    }
    GC_TRACE_MARK(4, obj);
    obj->isDark = true;
    INC_GEN(obj);
    // add object to gray stack
    if (vm.grayCapacity < vm.grayCount+1) {
        GC_TRACE_DEBUG(5, "Allocating more space for grayStack");
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        // Not using reallocate() here because we don't want to trigger the GC
        // inside a GC!
        vm.grayStack = realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
        ASSERT_MEM(vm.grayStack);
    }
    vm.grayStack[vm.grayCount++] = obj;
    TRACE_GC_FUNC_END(4, "grayObject");
}

void grayValue(Value val) {
    if (!IS_OBJ(val)) return;
    TRACE_GC_FUNC_START(4, "grayValue");
    grayObject(AS_OBJ(val));
    TRACE_GC_FUNC_END(4, "grayValue");
}

static void grayArray(ValueArray *ary) {
    TRACE_GC_FUNC_START(5, "grayArray");
    for (int i = 0; i < ary->count; i++) {
        grayValue(ary->values[i]);
    }
    TRACE_GC_FUNC_END(5, "grayArray");
}

// recursively gray an object's references
void blackenObject(Obj *obj) {
    if (obj->type == OBJ_T_NONE) return;
    TRACE_GC_FUNC_START(4, "blackenObject");
    switch (obj->type) {
        case OBJ_T_BOUND_METHOD: {
            GC_TRACE_DEBUG(5, "Blackening bound method %p", obj);
            ObjBoundMethod *method = (ObjBoundMethod*)obj;
            grayValue(method->receiver);
            grayObject(method->callable);
            break;
        }
        case OBJ_T_CLASS: {
            GC_TRACE_DEBUG(5, "Blackening class %p", obj);
            ObjClass *klass = (ObjClass*)obj;
            if (klass->name) {
                grayObject((Obj*)klass->name);
            }
            if (klass->klass) {
                grayObject((Obj*)klass->klass);
            }
            if (klass->singletonKlass) {
                grayObject((Obj*)klass->singletonKlass);
            }
            if (klass->superclass) {
                grayObject((Obj*)klass->superclass);
            }
            if (klass->finalizerFunc) {
                grayObject(klass->finalizerFunc);
            }
            grayTable(klass->fields);
            grayTable(klass->hiddenFields);
            grayTable(klass->methods);
            grayTable(klass->getters);
            grayTable(klass->setters);
            break;
        }
        case OBJ_T_MODULE: {
            ObjModule *mod = (ObjModule*)obj;
            GC_TRACE_DEBUG(5, "Blackening module %p", mod);
            if (mod->klass) {
                GC_TRACE_DEBUG(8, "Graying module class");
                grayObject((Obj*)mod->klass);
            }
            if (mod->singletonKlass) {
                GC_TRACE_DEBUG(8, "Graying module singleton class");
                grayObject((Obj*)mod->singletonKlass);
            }
            if (mod->name) {
                GC_TRACE_DEBUG(8, "Graying module name");
                grayObject((Obj*)mod->name);
            }
            if (mod->finalizerFunc) {
                GC_TRACE_DEBUG(8, "Graying module finalizer");
                grayObject(mod->finalizerFunc);
            }

            grayTable(mod->fields);
            grayTable(mod->hiddenFields);
            grayTable(mod->methods);
            grayTable(mod->getters);
            grayTable(mod->setters);
            break;
        }
        case OBJ_T_FUNCTION: {
            GC_TRACE_DEBUG(5, "Blackening function %p", obj);
            ObjFunction *func = (ObjFunction*)obj;
            if (func->name) {
                grayObject((Obj*)func->name);
            }
            break;
        }
        case OBJ_T_CLOSURE: {
            GC_TRACE_DEBUG(5, "Blackening closure %p", obj);
            ObjClosure *closure = (ObjClosure*)obj;
            grayObject((Obj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                grayObject((Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_T_NATIVE_FUNCTION: {
            GC_TRACE_DEBUG(5, "Blackening native function %p", obj);
            ObjNative *native = (ObjNative*)obj;
            grayObject((Obj*)native->name);
            break;
        }
        case OBJ_T_INSTANCE: {
            GC_TRACE_DEBUG(5, "Blackening instance %p", obj);
            ObjInstance *instance = (ObjInstance*)obj;
            grayObject((Obj*)instance->klass);
            if (instance->singletonKlass) {
                grayObject((Obj*)instance->singletonKlass);
            }
            if (instance->finalizerFunc) {
                grayObject(instance->finalizerFunc);
            }
            grayTable(instance->fields);
            grayTable(instance->hiddenFields);
            break;
        }
        case OBJ_T_INTERNAL: {
            GC_TRACE_DEBUG(5, "Blackening internal object %p", obj);
            ObjInternal *internal = (ObjInternal*)obj;
            if (internal->markFunc) {
                internal->markFunc(obj);
            }
            break;
        }
        case OBJ_T_UPVALUE: {
            GC_TRACE_DEBUG(5, "Blackening upvalue object %p", obj);
            grayValue(((ObjUpvalue*)obj)->closed);
            break;
        }
        case OBJ_T_STRING: { // no references
            GC_TRACE_DEBUG(5, "Blackening internal string %p", obj);
            break;
        }
        default: {
            // XXX: this does happen sometimes when calling GC.collect() multiple times (4+).
            // Until I fix this, this return is necessary.
            UNREACHABLE("Unknown object type: %d", obj->type);
        }
    }
    TRACE_GC_FUNC_END(4, "blackenObject");
}


static size_t sizeofObj(Obj *obj) {
    return sizeofObjType(obj->type);
}

void freeObject(Obj *obj) {
    if (obj->type == OBJ_T_NONE) {
        GC_TRACE_DEBUG(5, "freeObject called on OBJ_T_NONE: %p", obj);
        return; // already freed
    }

    ASSERT(!obj->noGC);
    TRACE_GC_FUNC_START(4, "freeObject");
    if (vm.inited) {
        int stackObjFoundIdx = -1;
        vec_find(&vm.stackObjects, obj, stackObjFoundIdx);
        if (stackObjFoundIdx != -1) {
            if (inGC) {
                GC_TRACE_DEBUG(4, "Skipped freeing stack object: p=%p (in GC)", obj);
                return; // Don't free stack objects in a GC
            }
            GC_TRACE_DEBUG(5, "Freeing stack object: p=%p (must be manual call "
                "to freeObject(), not in GC)", obj);
            vec_splice(&vm.stackObjects, stackObjFoundIdx, 1);
        }

    }

    GC_TRACE_FREE(4, obj);

    GCStats.generations[obj->GCGen]--;
    GCStats.heapUsed -= sizeof(ObjAny);
    GCStats.heapUsedWaste -= (sizeof(ObjAny)-sizeofObj(obj));
    GCStats.demographics[obj->type]--;

    switch (obj->type) {
        case OBJ_T_BOUND_METHOD: {
            // NOTE: don't free the actual underlying function, we need this
            // to stick around if only the bound method needs freeing
            GC_TRACE_DEBUG(5, "Freeing bound method: p=%p", obj);
            obj->type = OBJ_T_NONE;
            break;
        }
        case OBJ_T_CLASS: {
            ObjClass *klass = (ObjClass*)obj;
            GC_TRACE_DEBUG(5, "Freeing class methods/getters/setters tables");
            klass->name = NULL;
            freeTable(klass->fields);
            freeTable(klass->hiddenFields);
            freeTable(klass->methods);
            freeTable(klass->getters);
            freeTable(klass->setters);
            FREE_ARRAY(Table, klass->fields, 5);
            vec_deinit(klass->v_includedMods);
            FREE_SIZE(VEC_SZ, klass->v_includedMods);
            GC_TRACE_DEBUG(5, "Freeing class: p=%p", obj);
            obj->type = OBJ_T_NONE;
            break;
        }
        case OBJ_T_MODULE: {
            ObjModule *mod = (ObjModule*)obj;
            GC_TRACE_DEBUG(5, "Freeing module methods/getters/setters tables");
            mod->name = NULL;
            freeTable(mod->fields);
            freeTable(mod->hiddenFields);
            freeTable(mod->methods);
            freeTable(mod->getters);
            freeTable(mod->setters);
            FREE_ARRAY(Table, mod->fields, 5);
            GC_TRACE_DEBUG(5, "Freeing module: p=%p", obj);
            obj->type = OBJ_T_NONE;
            break;
        }
        case OBJ_T_FUNCTION: {
            ObjFunction *func = (ObjFunction*)obj; (void)func;
            GC_TRACE_DEBUG(5, "Freeing ObjFunction chunk: p=%p", &func->chunk);
            // FIXME: right now, multiple function objects can refer to the same
            // chunk, due to how chunks are passed around and copied by value
            // (I think this is the reason). Freeing them right now results in
            // double free errors.
            /*freeChunk(&func->chunk);*/
            GC_TRACE_DEBUG(5, "Freeing ObjFunction: p=%p", obj);
            obj->type = OBJ_T_NONE;
            break;
        }
        case OBJ_T_CLOSURE: {
            ObjClosure *closure = (ObjClosure*)obj;
            GC_TRACE_DEBUG(5, "Freeing ObjClosure: p=%p", closure);
            FREE_ARRAY(Value, closure->upvalues, closure->upvalueCount);
            obj->type = OBJ_T_NONE;
            break;
        }
        case OBJ_T_NATIVE_FUNCTION: {
            GC_TRACE_DEBUG(5, "Freeing ObjNative: p=%p", obj);
            obj->type = OBJ_T_NONE;
            break;
        }
        case OBJ_T_INSTANCE: {
            ObjInstance *instance = (ObjInstance*)obj;
            GC_TRACE_DEBUG(5, "Freeing instance fields table: p=%p", &instance->fields);
            freeTable(instance->fields);
            GC_TRACE_DEBUG(5, "Freeing instance hidden fields table: p=%p", &instance->hiddenFields);
            freeTable(instance->hiddenFields);
            FREE_ARRAY(Table, instance->fields, 2);
            GC_TRACE_DEBUG(5, "Freeing ObjInstance: p=%p", obj);
            obj->type = OBJ_T_NONE;
            break;
        }
        case OBJ_T_INTERNAL: {
            ObjInternal *internal = (ObjInternal*)obj;
            if (internal->freeFunc) {
                GC_TRACE_DEBUG(5, "Freeing internal object's references: p=%p, datap=%p", internal, internal->data);
                internal->freeFunc(obj);
            } else if (internal->data) {
                GC_TRACE_DEBUG(5, "Freeing internal object data: p=%p", internal->data);
                ASSERT(internal->dataSz > 0);
                FREE_SIZE(internal->dataSz, internal->data);
            } else {
                ASSERT(0);
            }
            GC_TRACE_DEBUG(5, "Freeing internal object: p=%p", internal);
            obj->type = OBJ_T_NONE;
            break;
        }
        case OBJ_T_UPVALUE: {
            GC_TRACE_DEBUG(5, "Freeing upvalue: p=%p", obj);
            obj->type = OBJ_T_NONE;
            break;
        }
        case OBJ_T_STRING: {
            ObjString *string = (ObjString*)obj;
            ASSERT(string->chars);
            GC_TRACE_DEBUG(5, "Freeing string chars: p=%p, interned=%s,static=%s",
                    string->chars,
                    string->isInterned ? "t" : "f",
                    string->isStatic ? "t" : "f"
            );
            GC_TRACE_DEBUG(5, "Freeing string chars: s='%s' (len=%d, capa=%d)", string->chars, string->length, string->capacity);
            FREE_ARRAY(char, string->chars, string->capacity + 1);
            string->chars = NULL;
            string->hash = 0;
            GC_TRACE_DEBUG(5, "Freeing ObjString: p=%p", obj);
            obj->type = OBJ_T_NONE;
            break;
        }
        default: {
            UNREACHABLE("Unknown object type: %d", obj->type);
        }
    }
    TRACE_GC_FUNC_END(4, "freeObject");
}

// GC stats
static unsigned numRootsLastGC = 0;

bool turnGCOff(void) {
    GC_TRACE_DEBUG(5, "GC turned OFF");
    bool prevVal = GCOn;
    GCOn = false;
    return prevVal;
}
bool turnGCOn(void) {
    GC_TRACE_DEBUG(5, "GC turned ON");
    bool prevVal = GCOn;
    GCOn = true;
    return prevVal;
}

// Usage:
// bool prevGC = turnGCOff();
// ... do stuff ...
// setGCOnOff(prevGC);
void setGCOnOff(bool turnOn) {
    GC_TRACE_DEBUG(5, "GC turned back %s", turnOn ? "ON" : "OFF");
    GCOn = turnOn;
}

void hideFromGC(Obj *obj) {
    DBG_ASSERT(obj);
    if (!obj->noGC) {
        vec_push(&vm.hiddenObjs, obj);
        obj->noGC = true;
    }
}

void unhideFromGC(Obj *obj) {
    DBG_ASSERT(obj);
    if (obj->noGC) {
        vec_remove(&vm.hiddenObjs, obj);
        obj->noGC = false;
    }
}

// Don't free callable objects, because they might be used in finalizers.
// Also, don't free internal objects, because they might be used in
// finalizers by the object getting finalized, and don't free internal
// strings because they're used as keys into hidden fields to access
// the internal objects.
/*static bool skipFreeInPhase1(Obj *obj) {*/
    /*return isCallable(OBJ_VAL(obj)) || obj->type == OBJ_T_INTERNAL || obj->type == OBJ_T_STRING;*/
/*}*/

static bool hasFinalizer(Obj *obj) {
    if (obj->type != OBJ_T_INSTANCE && obj->type != OBJ_T_CLASS && obj->type != OBJ_T_MODULE) {
        return false;
    }
    ObjInstance *instance = (ObjInstance*)obj;
    return instance->finalizerFunc != NULL;
}

static void callFinalizer(Obj *obj) {
    ObjInstance *instance = (ObjInstance*)obj;
    Value instanceVal = OBJ_VAL(obj);
    GC_TRACE_DEBUG(3, "Calling finalizer");
    inGC = false; // so we can allocate objects in the function
    dontGC = true;
    callFunctionValue(OBJ_VAL(instance->finalizerFunc), 1, &instanceVal);
    inGC = true;
    dontGC = false;
    instance->finalizerFunc = NULL;
    activeFinalizers--;
}

// single-phase mark and sweep
// TODO: divide work up into mark and sweep phases to limit GC pauses
void collectGarbage(void) {
    if (!GCOn || OPTION_T(disableGC)) {
        GC_TRACE_DEBUG(1, "GC run skipped (GC OFF)");
        return;
    }
    if (inGC) {
        fprintf(stderr, "[BUG]: GC tried to start during a GC run?\n");
        ASSERT(0);
    }
    struct timeval tRunStart;
    startGCRunProfileTimer(&tRunStart);

    GC_TRACE_DEBUG(1, "Collecting garbage");
    inGC = true;
    size_t before = GCStats.totalAllocated; (void)before;
    GC_TRACE_DEBUG(2, "GC begin");

    GC_TRACE_DEBUG(2, "Marking finalizers");

    GC_TRACE_DEBUG(2, "Marking VM stack roots");
    if (GET_OPTION(traceGCLvl) >= 2) {
        printGCStats();
        if (GET_OPTION(traceGCLvl >= 4)) {
            printGenerationInfo();
        }
        printVMStack(stderr);
    }
    // Mark stack roots up the stack for every execution context
    VMExecContext *ctx = NULL; int k = 0;
    vec_foreach(&vm.v_ecs, ctx, k) {
        grayTable(&ctx->roGlobals);
        for (Value *slot = ctx->stack; slot < ctx->stackTop; slot++) {
            grayValue(*slot);
        }
    }

    GC_TRACE_DEBUG(2, "Marking VM C-call stack objects (%d found)", vm.stackObjects.length);
    Obj *stackObjPtr = NULL; int idx = 0;
    vec_foreach(&vm.stackObjects, stackObjPtr, idx) {
        grayObject(stackObjPtr);
    }

    Value *scriptName; int i = 0;
    vec_foreach_ptr(&vm.loadedScripts, scriptName, i) {
        grayValue(*scriptName);
    }

    GC_TRACE_DEBUG(2, "Marking VM frame functions");
    // gray active function closure objects
    ctx = NULL; k = 0;
    int numFramesFound = 0;
    vec_foreach(&vm.v_ecs, ctx, k) {
        grayObject((Obj*)ctx->filename);
        if (ctx->lastValue) {
            grayValue(*ctx->lastValue);
        }
        for (int i = 0; i < ctx->frameCount; i++) {
            grayObject((Obj*)ctx->frames[i].closure);
            grayObject((Obj*)ctx->frames[i].instance);
            numFramesFound++;
        }
    }
    GC_TRACE_DEBUG(2, "%d frame functions found", numFramesFound);

    GC_TRACE_DEBUG(2, "Marking open upvalues");
    int numOpenUpsFound = 0;
    if (vm.openUpvalues) {
        ObjUpvalue *up = vm.openUpvalues;
        while (up) {
            ASSERT(up->value);
            grayValue(*up->value);
            up = up->next;
            numOpenUpsFound++;
        }
    }
    GC_TRACE_DEBUG(3, "Open upvalues found: %d", numOpenUpsFound);

    GC_TRACE_DEBUG(2, "Marking VM threads");
    ObjInstance *thread = NULL; int ti = 0;
    vec_foreach(&vm.threads, thread, ti) {
        grayObject((Obj*)thread);
    }

    GC_TRACE_DEBUG(2, "Marking globals (%d found)", vm.globals.count);
    grayTable(&vm.globals);
    GC_TRACE_DEBUG(2, "Marking interned strings (%d found)", vm.strings.count);
    grayTable(&vm.strings);
    GC_TRACE_DEBUG(2, "Marking compiler roots");
    grayCompilerRoots();
    GC_TRACE_DEBUG(3, "Marking VM cached strings");
    grayObject((Obj*)vm.initString);
    grayObject((Obj*)vm.fileString);
    grayObject((Obj*)vm.dirString);
    if (vm.printBuf) {
        GC_TRACE_DEBUG(3, "Marking VM print buf");
        grayObject((Obj*)vm.printBuf);
    }

    if (vm.lastValue != NULL) {
        GC_TRACE_DEBUG(3, "Marking VM last value");
        grayValue(*vm.lastValue);
    }

    GC_TRACE_DEBUG(2, "Marking atExit handlers: %d", vm.exitHandlers.length);
    ObjClosure *func = NULL;
    int funcIdx = 0;
    vec_foreach(&vm.exitHandlers, func, funcIdx) {
        grayObject((Obj*)func);
    }

    GC_TRACE_DEBUG(2, "Marking VM hidden rooted objects (%d)", vm.hiddenObjs.length);
    // gray hidden roots...
    void *objPtr = NULL; int j = 0;
    int numHiddenRoots = vm.hiddenObjs.length;
    int numHiddenFound = 0;
    vec_foreach(&vm.hiddenObjs, objPtr, j) {
        if (((Obj*)objPtr)->noGC) {
            GC_TRACE_DEBUG(5, "Hidden root found: %p", objPtr);
            /*GC_TRACE_MARK(3, objPtr);*/
            numHiddenFound++;
            grayObject((Obj*)objPtr);
        } else {
            GC_TRACE_DEBUG(3, "Non-hidden root found in hiddenObjs list: %p", objPtr);
            ASSERT(0);
        }
    }
    GC_TRACE_DEBUG(3, "Hidden roots founds: %d", numHiddenFound);

    ASSERT(numHiddenFound == numHiddenRoots);

    if (numHiddenFound < numHiddenRoots) {
        fprintf(stderr, "GC ERR: Hidden roots found: %d, hidden roots: %d\n",
            numHiddenFound, numHiddenRoots);
        ASSERT(0);
    }

    numRootsLastGC = vm.grayCount;

    GC_TRACE_DEBUG(2, "Blackening marked references");
    // traverse the references, graying them all
    while (vm.grayCount > 0) {
        // Pop an item from the gray stack.
        Obj *marked = vm.grayStack[--vm.grayCount];
        DBG_ASSERT(marked);
        blackenObject(marked);
    }
    GC_TRACE_DEBUG(3, "Done blackening references");

    GC_TRACE_DEBUG(2, "Begin FREE process");
    // Collect the white (unmarked) objects.
    int iter = 0; (void)iter;
    unsigned long numObjectsFreed = 0;
    unsigned long numObjectsKept = 0;
    unsigned long numObjectsHiddenNotMarked = 0; // got saved by NoGC flag

    ObjAny *p, *pend;

    int phase = 1; // call finalizers in phase 1
    vec_void_t vFreeHeaps;
    vec_init(&vFreeHeaps);
    freeList = NULL; // build up a new free list in phase 2
    if (activeFinalizers == 0) {
        phase = 2;
    }
    bool hasOtherFreeishHeap = false;
freeLoop:
    for (int i = 0; i < heapsUsed; i++) {
        ObjAny *newFreeList = NULL;
        if (phase == 2) newFreeList = freeList;
        p = heapList[i];
        ASSERT(p);
        pend = p + HEAP_SLOTS;

        int objectsFree = 0;
        while (p < pend) {
            Obj *obj = (Obj*)p;
            if (obj->type == OBJ_T_NONE) {
                if (phase == 2) objectsFree++;
                p++;
                continue;
            }
            if (!obj->isDark && !obj->noGC) {
                if (phase == 2) { // phase 2, reclaim unmarked objects
                    numObjectsFreed++;
                    obj->nextFree = newFreeList;
                    freeObject(obj);
                    newFreeList = p;
                    numObjectsFreed++;
                } else { // phase 1, call finalizers
                    ASSERT(phase == 1);
                    if (hasFinalizer(obj)) {
                        ASSERT(((ObjInstance*) obj)->finalizerFunc->type != OBJ_T_NONE);
                        callFinalizer(obj);
                        if (activeFinalizers == 0) {
                            phase = 2; i = 0;
                            goto freeLoop;
                        }
                    }
                }
            } else if (obj->noGC && !obj->isDark) { // keep
                if (phase == 2) {
                    numObjectsHiddenNotMarked++;
                }
            } else { // unmark for next run
                if (phase == 2) {
                    obj->isDark = false;
                    numObjectsKept++;
                }
            }
            p++;
        }

        if (phase == 2) {
            freeList = newFreeList;
            if (objectsFree == HEAP_SLOTS) {
                vec_push(&vFreeHeaps, heapList[i]);
            } else if (objectsFree >= (HEAP_SLOTS/2)) {
                hasOtherFreeishHeap = true;
            }
        }

    }
    if (phase == 1) {
        phase = 2; i = 0;
        freeList = NULL;
        goto freeLoop;
    }

    bool freedHeap = false;
    if (vFreeHeaps.length > 0 && hasOtherFreeishHeap) {
        ObjAny *heap; int heapIdx = 0;
        vec_foreach(&vFreeHeaps, heap, heapIdx) {
            ASSERT(heap);
            freeHeap(heap);
        }
        freedHeap = true;
    }

    if (!freedHeap && numObjectsFreed < FREE_MIN) {
        addHeap();
    }

    GC_TRACE_DEBUG(2, "done FREE process");
    GC_TRACE_DEBUG(3, "%lu objects freed, %lu objects kept, %lu unmarked hidden objects",
            numObjectsFreed, numObjectsKept, numObjectsHiddenNotMarked);

    GC_TRACE_DEBUG(3, "Collected %ld KB (from %ld to %ld)",
        (before - GCStats.totalAllocated)/1024, before/1024, GCStats.totalAllocated/1024);
    GC_TRACE_DEBUG(3, "Stats: roots found: %d, hidden roots found: %d",
        numRootsLastGC, numHiddenRoots);
    GC_TRACE_DEBUG(1, "Done collecting garbage");
    stopGCRunProfileTimer(&tRunStart);
    GCProf.totalRuns++;
    inGC = false;
}


// Force free all objects, regardless of noGC field on the object.
// Happens during VM shutdown.
void freeObjects(void) {
    GC_TRACE_DEBUG(2, "freeObjects -> begin FREEing all objects");
    if (GET_OPTION(traceGCLvl) >= 2) {
        printGCStats();
        printGenerationInfo();
    }
    struct timeval tRunStart;
    startGCRunProfileTimer(&tRunStart);

    ObjAny *p, *pend;

    int phase = 1;
    if (activeFinalizers == 0) {
        phase = 2;
    }

freeLoop:
    for (int i = 0; i < heapsUsed; i++) {
        p = heapList[i];
        pend = p + HEAP_SLOTS;

        while (p < pend) {
            Obj *obj = (Obj*)p;
            if (obj->type == OBJ_T_NONE) {
                p++;
                continue;
            }
            if (phase == 2) {
                if (obj->noGC) {
                    unhideFromGC(obj);
                }
                freeObject(obj);
            } else { // phase 1
                if (hasFinalizer(obj)) {
                    ASSERT(((ObjInstance*) obj)->finalizerFunc->type != OBJ_T_NONE);
                    callFinalizer(obj);
                    if (activeFinalizers == 0) {
                        phase = 2; i = 0;
                        goto freeLoop;
                    }
                }
            }
            p++;
        }

        if (phase == 2) {
            xfree(heapList[i]); // free the heap
            GCStats.totalAllocated -= (sizeof(ObjAny)*HEAP_SLOTS);
            GCStats.heapSize -= (sizeof(ObjAny)*HEAP_SLOTS);
        }
    }
    if (phase == 1) {
        phase = 2;
        goto freeLoop;
    }

    if (heapList) {
        xfree(heapList);
        GCStats.totalAllocated -= (sizeof(void*)*heapListSize);
    }
    heapList = NULL;
    heapsUsed = 0;
    heapListSize = 0;
    freeList = NULL;

    if (vm.grayStack) {
        xfree(vm.grayStack);
        vm.grayStack = NULL;
        vm.grayCount = 0;
        vm.grayCapacity = 0;
    }
    GC_TRACE_DEBUG(2, "/freeObjects");
    numRootsLastGC = 0;
    stopGCRunProfileTimer(&tRunStart);
    GCProf.totalRuns++;

    ASSERT(GCStats.heapSize == 0);
    ASSERT(GCStats.heapUsed == 0);
    ASSERT(GCStats.heapUsedWaste == 0);
    inGC = false;
}
