#include <stdlib.h>

#include "common.h"
#include "memory.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"
#include "value.h"
#include "options.h"

#ifdef NDEBUG
#define GC_TRACE_MARK(obj)
#define GC_TRACE_FREE(obj)
#define GC_TRACE_DEBUG(...)
#define TRACE_GC_FUNC_START(func)
#define TRACE_GC_FUNC_END(func)
#else
#define GC_TRACE_MARK(obj) gc_trace_mark(obj)
#define GC_TRACE_FREE(obj) gc_trace_free(obj)
#define GC_TRACE_DEBUG(...) gc_trace_debug(__VA_ARGS__)
#define TRACE_GC_FUNC_START(func) trace_gc_func_start(func);
#define TRACE_GC_FUNC_END(func) trace_gc_func_end(func);
#endif

static inline void gc_trace_mark(Obj *obj) {
    if (!CLOX_OPTION_T(traceGC)) return;
    fprintf(stderr, "[GC]: marking object at %p, ", obj);
    fprintf(stderr, "value => ");
    printValue(stderr, OBJ_VAL(obj));
    fprintf(stderr, "\n");
}

static inline void gc_trace_free(Obj *obj) {
    if (!CLOX_OPTION_T(traceGC)) return;
    fprintf(stderr, "[GC]: freeing object at %p, ", obj);
    fprintf(stderr, "type => %s , value => ", typeOfObj(obj));
    printValue(stderr, OBJ_VAL(obj));
    fprintf(stderr, "\n");
}

static inline void gc_trace_debug(const char *fmt, ...) {
    if (!CLOX_OPTION_T(traceGC)) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[GC]: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static inline void trace_gc_func_start(const char *funcName) {
    if (!CLOX_OPTION_T(traceGC)) return;
    fprintf(stderr, "[GC]: <%s>\n", funcName);
}
static inline void trace_gc_func_end(const char *funcName) {
    if (!CLOX_OPTION_T(traceGC)) return;
    fprintf(stderr, "[GC]: </%s>\n", funcName);
}

void *reallocate(void *previous, size_t oldSize, size_t newSize) {
    TRACE_GC_FUNC_START("reallocate");
    vm.bytesAllocated += (newSize - oldSize);

    if (vm.bytesAllocated > vm.nextGCThreshhold && newSize > oldSize) {
        collectGarbage();
    }

    if (newSize == 0) {
        GC_TRACE_DEBUG("freeing %p from realloc", previous);
        free(previous);
        TRACE_GC_FUNC_END("reallocate");
        return NULL;
    }

    void *ret = realloc(previous, newSize);
    ASSERT_MEM(ret);
    TRACE_GC_FUNC_END("reallocate");
    return ret;
}

void grayObject(Obj *obj) {
    TRACE_GC_FUNC_START("grayObject");
    if (obj == NULL) {
        TRACE_GC_FUNC_END("grayObject (null)");
        return;
    }
    if (obj->isDark) {
        TRACE_GC_FUNC_END("grayObject (dark)");
        return;
    }
    GC_TRACE_MARK(obj);
    obj->isDark = true;
    // add object to gray stack
    if (vm.grayCapacity < vm.grayCount+1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        // Not using reallocate() here because we don't want to trigger the GC
        // inside a GC!
        vm.grayStack = realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
    }
    vm.grayStack[vm.grayCount++] = obj;
    TRACE_GC_FUNC_END("grayObject");
}

void grayValue(Value val) {
    TRACE_GC_FUNC_START("grayValue");
    grayObject(AS_OBJ(val));
    TRACE_GC_FUNC_END("grayValue");
}

static void grayArray(ValueArray *ary) {
    TRACE_GC_FUNC_START("grayArray");
    for (int i = 0; i < ary->count; i++) {
        grayValue(ary->values[i]);
    }
    TRACE_GC_FUNC_END("grayArray");
}

// recursively gray an object's references
static void blackenObject(Obj *obj) {
    TRACE_GC_FUNC_START("blackenObject");
    switch (obj->type) {
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *method = (ObjBoundMethod*)obj;
            grayValue(method->receiver);
            grayObject((Obj*)method->method);
            break;
        }
        case OBJ_CLASS: {
            ObjClass *klass = (ObjClass*)obj;
            grayObject((Obj*)klass->name);
            grayObject((Obj*)klass->superclass);
            grayTable(&klass->methods);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction *func = (ObjFunction*)obj;
            grayObject((Obj*)func->name);
            break;
        }
        case OBJ_NATIVE_FUNCTION: {
            ObjNative *native = (ObjNative*)obj;
            grayObject((Obj*)native->name);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance *instance = (ObjInstance*)obj;
            grayObject((Obj*)instance->klass);
            grayTable(&instance->fields);
            break;
        }
        case OBJ_STRING: { // no references
            break;
        }
        default: {
            ASSERT(0);
        }
    }
    TRACE_GC_FUNC_END("blackenObject");
}

void freeObject(Obj *obj) {
    if (obj == NULL) return;
    ASSERT(!obj->noGC);
    TRACE_GC_FUNC_START("freeObject");
    GC_TRACE_FREE(obj);
    switch (obj->type) {
        case OBJ_BOUND_METHOD: {
            // NOTE: don't free the actual underlying function, we need this
            // to stick around if only the bound method needs freeing
            GC_TRACE_DEBUG("Freeing bound method: p=%p", obj);
            FREE(ObjBoundMethod, obj);
            break;
        }
        case OBJ_CLASS: {
            ObjClass *klass = (ObjClass*)obj;
            GC_TRACE_DEBUG("Freeing class method table");
            freeTable(&klass->methods);
            GC_TRACE_DEBUG("Freeing class: p=%p", obj);
            FREE(ObjClass, obj);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction *func = (ObjFunction*)obj;
            GC_TRACE_DEBUG("Freeing ObjFunction chunk: p=%p", &func->chunk);
            freeChunk(&func->chunk);
            GC_TRACE_DEBUG("Freeing ObjFunction chunk: p=%p", obj);
            FREE(ObjFunction, obj);
            break;
        }
        case OBJ_NATIVE_FUNCTION: {
            GC_TRACE_DEBUG("Freeing ObjNative: p=%p", obj);
            FREE(ObjNative, obj);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance *instance = (ObjInstance*)obj;
            GC_TRACE_DEBUG("Freeing instance fields table: p=%p", &instance->fields);
            freeTable(&instance->fields);
            GC_TRACE_DEBUG("Freeing ObjInstance: p=%p", obj);
            FREE(ObjInstance, obj);
            break;
        }
        case OBJ_STRING: {
            ObjString *string = (ObjString*)obj;
            GC_TRACE_DEBUG("Freeing string chars: p=%p", string->chars);
            GC_TRACE_DEBUG("Freeing string chars: s=%s", string->chars);
            FREE_ARRAY(char, string->chars, string->length + 1);
            GC_TRACE_DEBUG("Freeing ObjString: p=%p", obj);
            FREE(ObjString, obj);
            break;
        }
        default: {
            ASSERT(0);
        }
    }
    TRACE_GC_FUNC_END("freeObject");
}

static bool inGC = false;
static bool GCOn = true;

void turnGCOff(void) {
    GCOn = false;
}
void turnGCOn(void) {
    GCOn = true;
}

void hideFromGC(Obj *obj) {
    obj->noGC = true;
}

void unhideFromGC(Obj *obj) {
    obj->noGC = false;
}

// single-phase mark and sweep
// TODO: divide work up into mark and sweep phases to limit GC pauses
void collectGarbage(void) {
    if (!GCOn) {
        if (CLOX_OPTION_T(traceGC)) {
            fprintf(stderr, "GC run skipped (GC OFF)\n");
        }
        return;
    }
    if (inGC) {
        fprintf(stderr, "GC tried to start during a GC run?\n");
        ASSERT(0);
    }
    inGC = true;
    size_t before = vm.bytesAllocated;
    if (CLOX_OPTION_T(traceGC)) {
        fprintf(stderr, "[GC]: begin\n");
    }

    // Mark stack roots up the stack
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
        grayValue(*slot);
    }

    // gray active function objects
    for (int i = 0; i < vm.frameCount; i++) {
        grayObject((Obj*)vm.frames[i].function);
    }

    // Mark the global roots.
    grayTable(&vm.globals);
    grayTable(&vm.strings);
    grayCompilerRoots();
    grayObject((Obj*)vm.initString);
    grayObject((Obj*)vm.printBuf);

    if (vm.lastValue != NULL) {
        grayValue(*vm.lastValue);
    }

    // traverse the references, graying them all
    while (vm.grayCount > 0) {
        // Pop an item from the gray stack.
        Obj *marked = vm.grayStack[--vm.grayCount];
        blackenObject(marked);
    }

    // Delete unused interned strings.
    /*tableRemoveWhite(&vm.strings);*/

    // Collect the white (unmarked) objects.
    Obj **object = &vm.objects;
    while (*object != NULL) {
        if (!((*object)->isDark) && !((*object)->noGC)) {
            // This object wasn't reached, so remove it from the list and free it.
            Obj *unreached = *object;
            *object = unreached->next;
            freeObject(unreached);
        } else {
            // This object was reached, so unmark it (for the next GC) and move on to
            // the next.
            (*object)->isDark = false;
            object = &(*object)->next;
        }
    }

    // Adjust the heap size based on live memory.
    vm.nextGCThreshhold = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

    if (CLOX_OPTION_T(traceGC)) {
        fprintf(stderr, "[GC]: collected %ld bytes (from %ld to %ld) next GC at %ld bytes\n",
            before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGCThreshhold);
    }
    inGC = false;
}


void freeObjects(void) {
  Obj *object = vm.objects;
  while (object != NULL) {
    Obj *next = object->next;
    if (!object->noGC) freeObject(object);
    object = next;
  }

  if (vm.grayStack) free(vm.grayStack);
}
