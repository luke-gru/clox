#include <stdlib.h>

#include "common.h"
#include "memory.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"
#include "value.h"
#include "options.h"

#ifdef NDEBUG
#define GC_TRACE_MARK(obj) (void(0))
#define GC_TRACE_FREE(obj) (void(0))
#define GC_TRACE_DEBUG(...) (void(0))
#define TRACE_GC_FUNC_START(func) (void(0))
#define TRACE_GC_FUNC_END(func)   (void(0))
#else
#define GC_TRACE_MARK(obj) gc_trace_mark(obj)
#define GC_TRACE_FREE(obj) gc_trace_free(obj)
#define GC_TRACE_DEBUG(...) gc_trace_debug(__VA_ARGS__)
#define TRACE_GC_FUNC_START(func) trace_gc_func_start(func);
#define TRACE_GC_FUNC_END(func) trace_gc_func_end(func);
#endif

static void gc_trace_mark(Obj *obj) {
    if (!CLOX_OPTION_T(traceGC)) return;
    fprintf(stderr, "[GC]: marking object at %p, ", obj);
    fprintf(stderr, "value => ");
    printValue(stderr, OBJ_VAL(obj), false);
    fprintf(stderr, "\n");
}

static void gc_trace_free(Obj *obj) {
    if (!CLOX_OPTION_T(traceGC)) return;
    fprintf(stderr, "[GC]: freeing object at %p, ", obj);
    fprintf(stderr, "type => %s, value => ", typeOfObj(obj));
    printValue(stderr, OBJ_VAL(obj), false);
    fprintf(stderr, "\n");
}

static void gc_trace_debug(const char *fmt, ...) {
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

static bool inGC = false;
static bool GCOn = true;

// Main memory management function used by both ALLOCATE/FREE (see memory.h)
// NOTE: memory is NOT initialized (see man 3 realloc)
void *reallocate(void *previous, size_t oldSize, size_t newSize) {
    TRACE_GC_FUNC_START("reallocate");
    if (newSize > 0) {
        ASSERT(!inGC);
    }
    vm.bytesAllocated += (newSize - oldSize);

    if (vm.bytesAllocated > vm.nextGCThreshhold && newSize > oldSize) {
        collectGarbage();
    }

    if (newSize == 0) { // freeing
        GC_TRACE_DEBUG("  freeing %p from realloc", previous);
        free(previous);
        TRACE_GC_FUNC_END("reallocate");
        return NULL;
    }

    void *ret = realloc(previous, newSize);
    ASSERT_MEM(ret);
    if (newSize > 0) {
        GC_TRACE_DEBUG("  allocated %p", ret);
    }
    TRACE_GC_FUNC_END("reallocate");
    return ret;
}

void grayObject(Obj *obj) {
    TRACE_GC_FUNC_START("grayObject");
    if (obj == NULL) {
        TRACE_GC_FUNC_END("grayObject (null obj found)");
        return;
    }
    if (obj->isDark) {
        TRACE_GC_FUNC_END("grayObject (already dark)");
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
    if (!IS_OBJ(val)) return;
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
void blackenObject(Obj *obj) {
    TRACE_GC_FUNC_START("blackenObject");
    switch (obj->type) {
        case OBJ_T_BOUND_METHOD: {
            ObjBoundMethod *method = (ObjBoundMethod*)obj;
            grayValue(method->receiver);
            grayObject(method->callable);
            break;
        }
        case OBJ_T_CLASS: {
            ObjClass *klass = (ObjClass*)obj;
            grayObject((Obj*)klass->name);
            if (klass->klass) {
                grayObject((Obj*)klass->klass);
            }
            if (klass->singletonKlass) {
                grayObject((Obj*)klass->singletonKlass);
            }
            if (klass->superclass) {
                grayObject((Obj*)klass->superclass);
            }
            grayTable(&klass->fields);
            grayTable(&klass->hiddenFields);
            grayTable(&klass->methods);
            grayTable(&klass->getters);
            grayTable(&klass->setters);
            break;
        }
        case OBJ_T_FUNCTION: {
            ObjFunction *func = (ObjFunction*)obj;
            grayObject((Obj*)func->name);
            break;
        }
        case OBJ_T_CLOSURE: {
            ObjClosure *closure = (ObjClosure*)obj;
            grayObject((Obj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                grayObject((Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_T_NATIVE_FUNCTION: {
            ObjNative *native = (ObjNative*)obj;
            grayObject((Obj*)native->name);
            break;
        }
        case OBJ_T_INSTANCE: {
            ObjInstance *instance = (ObjInstance*)obj;
            grayObject((Obj*)instance->klass);
            if (instance->singletonKlass) {
                grayObject((Obj*)instance->singletonKlass);
            }
            grayTable(&instance->fields);
            grayTable(&instance->hiddenFields);
            break;
        }
        case OBJ_T_INTERNAL: {
            ObjInternal *internal = (ObjInternal*)obj;
            if (internal->markFunc) {
                internal->markFunc(obj);
            }
            break;
        }
        case OBJ_T_UPVALUE: {
            grayValue(((ObjUpvalue*)obj)->closed);
            break;
        }
        case OBJ_T_STRING: { // no references
            break;
        }
        default: {
            UNREACHABLE("Unknown object type: %d", obj->type);
        }
    }
    TRACE_GC_FUNC_END("blackenObject");
}

// NOTE: Doesn't unlink the object from vm.objects!
// TODO: make sure it unlinks, as manual calls to freeObject() should
// be okay.
void freeObject(Obj *obj, bool unlink) {
    if (obj == NULL) return;
    if (obj->type == OBJ_T_NONE) {
        return; // already freed
    }

    ASSERT(!obj->noGC);
    TRACE_GC_FUNC_START("freeObject");
    if (vm.inited) {
        int stackObjFoundIdx = -1;
        vec_find(&vm.stackObjects, obj, stackObjFoundIdx);
        if (stackObjFoundIdx != -1) {
            if (inGC) {
                GC_TRACE_DEBUG("Skipped freeing stack object: p=%p (in GC)", obj);
                return; // Don't free stack objects in a GC
            }
            GC_TRACE_DEBUG("Freeing stack object: p=%p (must be manual call "
                "to freeObject(), not in GC)", obj);
            vec_splice(&vm.stackObjects, stackObjFoundIdx, 1);
        }

    }

    GC_TRACE_FREE(obj);

    if (unlink) {
        ASSERT(obj->isLinked);
        GC_TRACE_DEBUG("Unlinking object p=%p", obj);
        Obj *next = obj->next;
        Obj *prev = obj->prev;
        if (next) {
            obj->next->prev = prev;
            obj->next = NULL;
        }
        if (prev) {
            obj->prev->next = next;
            obj->prev = NULL;
        } else {
            GC_TRACE_DEBUG("  unlinked first object");
            vm.objects = next;
        }
        obj->isLinked = false;
        if (next == NULL && prev == NULL) { // must be the only object
            GC_TRACE_DEBUG("  unlinked last object");
            vm.objects = NULL;
        }
    }

    switch (obj->type) {
        case OBJ_T_BOUND_METHOD: {
            // NOTE: don't free the actual underlying function, we need this
            // to stick around if only the bound method needs freeing
            GC_TRACE_DEBUG("Freeing bound method: p=%p", obj);
            FREE(ObjBoundMethod, obj);
            memset(obj, 0, sizeof(ObjBoundMethod));
            break;
        }
        case OBJ_T_CLASS: {
            ObjClass *klass = (ObjClass*)obj;
            GC_TRACE_DEBUG("Freeing class methods/getters/setters tables");
            freeTable(&klass->fields);
            freeTable(&klass->hiddenFields);
            freeTable(&klass->methods);
            freeTable(&klass->getters);
            freeTable(&klass->setters);
            GC_TRACE_DEBUG("Freeing class: p=%p", obj);
            FREE(ObjClass, obj);
            break;
        }
        case OBJ_T_FUNCTION: {
            ObjFunction *func = (ObjFunction*)obj;
            GC_TRACE_DEBUG("Freeing ObjFunction chunk: p=%p", &func->chunk);
            // FIXME: right now, multiple function objects can refer to the same
            // chunk, due to how chunks are passed around and copied by value
            // (I think this is the reason). Freeing them right now results in
            // double free errors.
            /*freeChunk(&func->chunk);*/
            GC_TRACE_DEBUG("Freeing ObjFunction: p=%p", obj);
            FREE(ObjFunction, obj);
            break;
        }
        case OBJ_T_CLOSURE: {
            ObjClosure *closure = (ObjClosure*)obj;
            GC_TRACE_DEBUG("Freeing ObjClosure: p=%p", closure);
            FREE_ARRAY(Value, closure->upvalues, closure->upvalueCount);
            FREE(ObjClosure, obj);
            break;
        }
        case OBJ_T_NATIVE_FUNCTION: {
            GC_TRACE_DEBUG("Freeing ObjNative: p=%p", obj);
            FREE(ObjNative, obj);
            break;
        }
        case OBJ_T_INSTANCE: {
            ObjInstance *instance = (ObjInstance*)obj;
            GC_TRACE_DEBUG("Freeing instance fields table: p=%p", &instance->fields);
            freeTable(&instance->fields);
            GC_TRACE_DEBUG("Freeing instance hidden fields table: p=%p", &instance->hiddenFields);
            freeTable(&instance->hiddenFields);
            GC_TRACE_DEBUG("Freeing ObjInstance: p=%p", obj);
            FREE(ObjInstance, obj);
            break;
        }
        case OBJ_T_INTERNAL: {
            ObjInternal *internal = (ObjInternal*)obj;
            if (internal->freeFunc) {
                GC_TRACE_DEBUG("Freeing internal object's references: p=%p, datap=%p", internal, internal->data);
                internal->freeFunc(obj);
            }
            GC_TRACE_DEBUG("Freeing internal object: p=%p", internal);
            FREE(ObjInternal, internal);
            break;
        }
        case OBJ_T_UPVALUE: {
            GC_TRACE_DEBUG("Freeing upvalue: p=%p", obj);
            FREE(ObjUpvalue, obj);
            break;
        }
        case OBJ_T_STRING: {
            ObjString *string = (ObjString*)obj;
            ASSERT(string->chars);
            GC_TRACE_DEBUG("Freeing string chars: p=%p", string->chars);
            GC_TRACE_DEBUG("Freeing string chars: s=%s", string->chars);
            FREE_ARRAY(char, string->chars, string->length + 1);
            string->chars = NULL;
            GC_TRACE_DEBUG("Freeing ObjString: p=%p", obj);
            FREE(ObjString, obj);
            memset(obj, 0, sizeof(ObjString));
            break;
        }
        default: {
            UNREACHABLE("Unknown object type: %d", obj->type);
        }
    }
    TRACE_GC_FUNC_END("freeObject");
}

// GC stats
static unsigned numRootsLastGC = 0;

bool turnGCOff(void) {
    GC_TRACE_DEBUG("GC turned OFF");
    bool prevVal = GCOn;
    GCOn = false;
    return prevVal;
}
bool turnGCOn(void) {
    GC_TRACE_DEBUG("GC turned ON");
    bool prevVal = GCOn;
    GCOn = true;
    return prevVal;
}

// Usage:
// bool prevGC = turnGCOff();
// ... do stuff ...
// setGCOnOff(prevGC);
void setGCOnOff(bool turnOn) {
    GC_TRACE_DEBUG("GC turned back %s", turnOn ? "ON" : "OFF");
    GCOn = turnOn;
}

void hideFromGC(Obj *obj) {
    if (!obj->noGC) {
        vec_push(&vm.hiddenObjs, obj);
        obj->noGC = true;
    }
}

void unhideFromGC(Obj *obj) {
    if (obj->noGC) {
        vec_remove(&vm.hiddenObjs, obj);
        obj->noGC = false;
    }
}

// single-phase mark and sweep
// TODO: divide work up into mark and sweep phases to limit GC pauses
void collectGarbage(void) {
    if (!GCOn) {
        GC_TRACE_DEBUG("GC run skipped (GC OFF)");
        return;
    }
    if (inGC) {
        fprintf(stderr, "GC tried to start during a GC run?\n");
        ASSERT(0);
    }
    inGC = true;
    size_t before = vm.bytesAllocated;
    GC_TRACE_DEBUG("GC begin");

    GC_TRACE_DEBUG("Marking VM stack roots");
    if (CLOX_OPTION_T(traceGC)) printVMStack(stderr);
    // Mark stack roots up the stack for every execution context
    VMExecContext *ctx = NULL; int k = 0;
    vec_foreach_ptr(&vm.v_ecs, ctx, k) {
        grayTable(&ctx->roGlobals);
        for (Value *slot = ctx->stack; slot < ctx->stackTop; slot++) {
            grayValue(*slot);
        }
    }

    GC_TRACE_DEBUG("Marking VM C stack objects (%d found)", vm.stackObjects.length);
    Obj *stackObjPtr = NULL; int idx = 0;
    vec_foreach(&vm.stackObjects, stackObjPtr, idx) {
        grayObject(stackObjPtr);
    }

    Value *scriptName; int i = 0;
    vec_foreach_ptr(&vm.loadedScripts, scriptName, i) {
        grayValue(*scriptName);
    }

    GC_TRACE_DEBUG("Marking VM frame functions");
    // gray active function closure objects
    ctx = NULL; k = 0;
    vec_foreach_ptr(&vm.v_ecs, ctx, k) {
        for (int i = 0; i < ctx->frameCount; i++) {
            grayObject((Obj*)ctx->frames[i].closure);
        }
    }

    GC_TRACE_DEBUG("Marking open upvalues");
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
    GC_TRACE_DEBUG("Open upvalues found: %d", numOpenUpsFound);

    GC_TRACE_DEBUG("Marking globals (%d found)", vm.globals.count);
    // Mark the global roots.
    grayTable(&vm.globals);
    GC_TRACE_DEBUG("Marking interned strings (%d found)", vm.strings.count);
    grayTable(&vm.strings);
    GC_TRACE_DEBUG("Marking compiler roots");
    grayCompilerRoots();
    GC_TRACE_DEBUG("Marking VM cached strings");
    grayObject((Obj*)vm.initString);
    grayObject((Obj*)vm.fileString);
    grayObject((Obj*)vm.dirString);
    if (vm.printBuf) {
        GC_TRACE_DEBUG("Marking VM print buf");
        grayObject((Obj*)vm.printBuf);
    }

    if (vm.lastValue != NULL) {
        GC_TRACE_DEBUG("Marking VM last value");
        grayValue(*vm.lastValue);
    }

    GC_TRACE_DEBUG("Marking VM hidden roots (%d)", vm.hiddenObjs.length);
    // gray hidden roots...
    void *objPtr = NULL; int j = 0;
    int numHiddenRoots = vm.hiddenObjs.length;
    int numHiddenFound = 0;
    vec_foreach(&vm.hiddenObjs, objPtr, j) {
        if (((Obj*)objPtr)->noGC) {
            GC_TRACE_DEBUG("Hidden root found: %p", objPtr);
            numHiddenFound++;
            grayObject((Obj*)objPtr);
        }
    }
    GC_TRACE_DEBUG("Hidden roots founds: %d", numHiddenFound);

    ASSERT(numHiddenFound == numHiddenRoots);

    if (numHiddenFound < numHiddenRoots) {
        fprintf(stderr, "GC ERR: Hidden roots found: %d, hidden roots: %d\n",
            numHiddenFound, numHiddenRoots);
        ASSERT(0);
    }

    numRootsLastGC = vm.grayCount;

    GC_TRACE_DEBUG("Traversing references...");
    // traverse the references, graying them all
    while (vm.grayCount > 0) {
        // Pop an item from the gray stack.
        Obj *marked = vm.grayStack[--vm.grayCount];
        blackenObject(marked);
    }

    // Delete unused interned strings.
    /*tableRemoveWhite(&vm.strings);*/

    GC_TRACE_DEBUG("Begin FREE process");
    // Collect the white (unmarked) objects.
    Obj *object = vm.objects;
    vec_void_t vvisited;
    vec_init(&vvisited);
    int iter = 0;
    while (object != NULL) {
        ASSERT(object->type > OBJ_T_NONE);
        int idx = 0;
        vec_find(&vvisited, object, idx);
        if (idx != -1) {
            const char *otypeStr = typeOfObj(object);
            GC_TRACE_DEBUG("Found cycle during free process (iter=%d, p=%p, otype=%s), stopping", iter, *object, otypeStr);
            break; // found cycles, dangerous (TODO: proper cycle detection)
        }
        vec_push(&vvisited, object);
        if (!object->isDark && !object->noGC) {
            // This object wasn't marked, so remove it from the list and free it.
            Obj *unreached = object;
            object = unreached->next;
            ASSERT(unreached->isLinked);
            freeObject(unreached, true);
            ASSERT(!unreached->isLinked);
        } else {
            // This object was reached, so unmark it (for the next GC) and move on to
            // the next.
            object->isDark = false;
            object = object->next;
        }
        iter++;
    }
    vec_deinit(&vvisited);
    GC_TRACE_DEBUG("done FREE process");

    // Adjust the heap size based on live memory.
    vm.nextGCThreshhold = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

    GC_TRACE_DEBUG("collected %ld bytes (from %ld to %ld) next GC at %ld bytes\n",
        before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGCThreshhold);
    GC_TRACE_DEBUG("stats: roots found: %d, hidden roots found: %d\n",
        numRootsLastGC, numHiddenRoots);
    inGC = false;
}


// Force free all objects, regardless of noGC field on the object,
// or whether it was created by the a C stack space allocation function.
void freeObjects(void) {
    GC_TRACE_DEBUG("freeObjects -> begin FREEing all objects");
    Obj *object = vm.objects;
    while (object != NULL) {
        Obj *next = object->next;
        if (object->noGC) {
            unhideFromGC(object);
        }
        // don't unlink because this is done in freeVM() anyway,
        // and this function should only be called from there.
        freeObject(object, false);
        object = next;
    }

    if (vm.grayStack) {
        free(vm.grayStack);
        vm.grayStack = NULL;
    }
    GC_TRACE_DEBUG("/freeObjects");
    numRootsLastGC = 0;
}
