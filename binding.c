#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"
#include "compiler.h"

ObjClass *lxBindingClass;

typedef struct LxBinding {
    CallFrame frame;
    CallFrame *origFrame;
    long origFrameCookie;
    Table localsTable;
} LxBinding;

static void populateLocalsTable(LxBinding *b) {
    Table *btbl = &b->localsTable;
    ASSERT(b->frame.closure);
    ObjFunction *func = b->frame.closure->function;
    ASSERT(func);
    Entry e; int idx = 0;
    TABLE_FOREACH(&func->localsTable, e, idx, {
        int slot = AS_NUMBER(e.value);
        tableSet(btbl, e.key, b->frame.slots[slot]);
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
    binding->origFrame = getFrame()->prev;
    binding->origFrameCookie = getFrame()->prev->cookie;
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
    if (binding->origFrame->popped || binding->origFrameCookie != binding->origFrame->cookie) {
        TABLE_FOREACH(&binding->localsTable, e, i, {
            mapSet(ret, e.key, e.value);
        });
    } else {
        ObjFunction *func = binding->frame.closure->function;
        int idx = 0;
        TABLE_FOREACH(&func->localsTable, e, idx, {
            int slot = AS_NUMBER(e.value);
            mapSet(ret, e.key, binding->origFrame->slots[slot]);
        });
    }
    return ret;
}

static Value lxBindingLocalVariableGet(int argCount, Value *args) {
    CHECK_ARITY("Binding#localVariableGet", 2, 2, argCount);
    Value self = *args;
    Value name = args[1];
    CHECK_ARG_IS_A(name, lxStringClass, 1);
    LxBinding *binding = getBinding(self);
    Value val;
    if (binding->origFrame->popped || binding->origFrameCookie != binding->origFrame->cookie) {
        if (tableGet(&binding->localsTable, name, &val)) {
            return val;
        } else {
            return NIL_VAL;
        }
    } else {
        ObjFunction *func = binding->frame.closure->function;
        Value slotVal;
        int res = tableGet(&func->localsTable, name, &slotVal);
        if (!res) { return NIL_VAL; }
        int slot = AS_NUMBER(slotVal);
        return binding->origFrame->slots[slot];
    }
}

static Value lxBindingLocalVariableSet(int argCount, Value *args) {
    CHECK_ARITY("Binding#localVariableSet", 3, 3, argCount);
    Value self = *args;
    Value name = args[1];
    CHECK_ARG_IS_A(name, lxStringClass, 1);
    Value val = args[2];
    LxBinding *binding = getBinding(self);
    if (binding->origFrame->popped || binding->origFrameCookie != binding->origFrame->cookie) {
        tableSet(&binding->localsTable, name, val);
    } else {
        ObjFunction *func = binding->frame.closure->function;
        Value slotVal;
        int res = tableGet(&func->localsTable, name, &slotVal);
        if (!res) { return val; }
        int slot = AS_NUMBER(slotVal);
        binding->origFrame->slots[slot] = val;
    }
    return val;
}

static Value lxBindingReceiver(int argCount, Value *args) {
    CHECK_ARITY("Binding#receiver", 1, 1, argCount);
    Value self = *args;
    LxBinding *binding = getBinding(self);
    if (binding->frame.instance) {
        return OBJ_VAL(binding->frame.instance);
    } else {
        return NIL_VAL;
    }
}

static Value lxBindingInspect(int argCount, Value *args) {
    CHECK_ARITY("Binding#inspect", 1, 1, argCount);
    Value self = *args;
    LxBinding *binding = getBinding(self);
    ObjString *ret = emptyString();
    pushCString(ret, "#<Binding ", 10);
    if (binding->frame.name) {
        pushCStringFmt(ret, "%s", binding->frame.name->chars);
    } else {
        pushCStringFmt(ret, "%s", "(anon)");
    }
    pushCString(ret, ">", 1);
    return OBJ_VAL(ret);
}

void Init_BindingClass(void) {
    lxBindingClass = addGlobalClass("Binding", lxObjClass);
    addNativeMethod(lxBindingClass, "init", lxBindingInit);
    addNativeMethod(lxBindingClass, "localVariables", lxBindingLocalVariables);
    addNativeMethod(lxBindingClass, "localVariableGet", lxBindingLocalVariableGet);
    addNativeMethod(lxBindingClass, "localVariableSet", lxBindingLocalVariableSet);
    addNativeMethod(lxBindingClass, "receiver", lxBindingReceiver);
    addNativeMethod(lxBindingClass, "inspect", lxBindingInspect);
}
