#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"
#include "compiler.h"

ObjClass *lxBindingClass;

typedef struct LxBinding {
  CallFrame frame;
  Table localsTable;
} LxBinding;

static void populateLocalsTable(LxBinding *b) {
  Table *btbl = &b->localsTable;
  ASSERT(b->frame.closure);
  ObjFunction *func = b->frame.closure->function;
  ASSERT(func);
  Entry e; int idx = 0;
  TABLE_FOREACH(&func->localsTable, e, idx, {
      int i = AS_NUMBER(e.value);
      tableSet(btbl, e.key, b->frame.slots[i]);
  });
}

static void markInternalBinding(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxBinding *b = (LxBinding*)internal->data;
    grayObject(TO_OBJ(b->frame.closure));
    grayObject(TO_OBJ(b->frame.name));
    if (b->frame.instance) {
      grayObject(TO_OBJ(b->frame.instance));
    }
    if (b->frame.klass) {
      grayObject(TO_OBJ(b->frame.klass));
    }
    if (b->frame.file) {
      grayObject(TO_OBJ(b->frame.file));
    }
    grayTable(&b->localsTable);
}

static void freeInternalBinding(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxBinding *b = (LxBinding*)internal->data;
    freeTable(&b->localsTable);
    FREE(LxBinding, b);
}

static Value lxBindingInit(int argCount, Value *args) {
    CHECK_ARITY("Binding#init", 1, 1, argCount);
    Value self = *args;
    ObjInstance *bindObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxBinding), markInternalBinding, freeInternalBinding,
            NEWOBJ_FLAG_NONE);
    LxBinding *binding = ALLOCATE(LxBinding, 1);
    binding->frame = *getFrame()->prev; // copy frame object
    ASSERT(!binding->frame.isCCall);
    initTable(&binding->localsTable);
    populateLocalsTable(binding);

    internalObj->data = binding;
    bindObj->internal = internalObj;
    unhideFromGC(TO_OBJ(internalObj));
    return self;
}

static LxBinding *getBinding(Value b) {
    ObjInstance *bindObj = AS_INSTANCE(b);
    ObjInternal *i = bindObj->internal;
    return (LxBinding*)i->data;
}

static Value lxBindingLocalVariables(int argCount, Value *args) {
    CHECK_ARITY("Binding#localVariables", 1, 1, argCount);
    Value self = *args;
    LxBinding *binding = getBinding(self);
    Value ret = newMap();
    Entry e; int i = 0;
    TABLE_FOREACH(&binding->localsTable, e, i, {
        mapSet(ret, e.key, e.value);
    });
    return ret;
}

void Init_BindingClass(void) {
    lxBindingClass = addGlobalClass("Binding", lxObjClass);
    addNativeMethod(lxBindingClass, "init", lxBindingInit);
    addNativeMethod(lxBindingClass, "localVariables", lxBindingLocalVariables);
}
