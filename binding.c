#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "table.h"
#include "memory.h"
#include "compiler.h"

ObjClass *lxBindingClass;

static void markInternalBinding(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxBinding *b = (LxBinding*)internal->data;
    grayObject(TO_OBJ(b->scope));
    ObjClass *klass; int kidx = 0;
    vec_foreach(&b->v_crefStack, klass, kidx) {
        grayObject(TO_OBJ(klass));
    }
    if (b->thisObj) {
        grayObject(b->thisObj);
    }
}

static void freeInternalBinding(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxBinding *b = (LxBinding*)internal->data;
    vec_deinit(&b->v_crefStack);
    FREE(LxBinding, b);
}

static Value lxBindingInit(int argCount, Value *args) {
    CHECK_ARITY("Binding#init", 1, 1, argCount);
    Value self = *args;
    ObjInstance *bindObj = AS_INSTANCE(self);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxBinding), markInternalBinding, freeInternalBinding,
            NEWOBJ_FLAG_NONE);
    LxBinding *binding = ALLOCATE(LxBinding, 1);
    CallFrame *frame = getFrame()->prev;
    binding->scope = frame->scope;

    LxThread *th = THREAD();
    vec_init(&binding->v_crefStack);
    ObjClass *klass; int kidx = 0;
    vec_foreach(&th->v_crefStack, klass, kidx) {
        vec_push(&binding->v_crefStack, klass);
    }
    (void)vec_pop(&binding->v_crefStack); // funny enough, `Binding` is atop the cref stack right now, we want it popped
    // Right now, `instance Binding` is the th->thisObj, so we get the one before that
    if (th->v_thisStack.length > 1) {
        binding->thisObj = th->v_thisStack.data[th->v_thisStack.length-2];
    } else {
        binding->thisObj = NULL;
    }

    ASSERT(!frame->isCCall);
    ASSERT(frame->scope);

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
    ObjFunction *func = binding->scope->function;
    Entry e; int idx = 0;
    TABLE_FOREACH(&func->localsTable, e, idx, {
        int slot = AS_NUMBER(e.value);
        mapSet(ret, e.key, binding->scope->localsTable.tbl[slot]);
    });
    return ret;
}

static Value lxBindingLocalVariableGet(int argCount, Value *args) {
    CHECK_ARITY("Binding#localVariableGet", 2, 2, argCount);
    Value self = *args;
    Value name = args[1];
    CHECK_ARG_IS_A(name, lxStringClass, 1);
    LxBinding *binding = getBinding(self);
    ObjFunction *func = binding->scope->function;
    Value slotVal;
    int res = tableGet(&func->localsTable, name, &slotVal);
    if (!res) { return NIL_VAL; }
    int slot = AS_NUMBER(slotVal);
    return binding->scope->localsTable.tbl[slot];
}

static Value lxBindingLocalVariableSet(int argCount, Value *args) {
    CHECK_ARITY("Binding#localVariableSet", 3, 3, argCount);
    Value self = *args;
    Value name = args[1];
    CHECK_ARG_IS_A(name, lxStringClass, 1);
    Value val = args[2];
    LxBinding *binding = getBinding(self);
    ObjFunction *func = binding->scope->function;
    Value slotVal;
    int res = tableGet(&func->localsTable, name, &slotVal);
    // FIXME: setting a localVariable shouldn't change the function object itself.
    // It should only change the current scope's mapping.
    if (!res) {
        slotVal = NUMBER_VAL(func->localsTable.count+1);
        tableSet(&func->localsTable, name, slotVal);
    }
    int slot = AS_NUMBER(slotVal);
    growLocalsTable(binding->scope, slot+1);
    binding->scope->localsTable.tbl[slot] = val;
    return val;
}

static Value lxBindingReceiver(int argCount, Value *args) {
    CHECK_ARITY("Binding#receiver", 1, 1, argCount);
    Value self = *args;
    LxBinding *binding = getBinding(self);
    ObjFunction *func = binding->scope->function;
    if (func->hasReceiver) {
        return binding->scope->localsTable.tbl[0];
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
    if (binding->scope->function->name) {
        pushCStringFmt(ret, "%s", binding->scope->function->name->chars);
    } else {
        pushCStringFmt(ret, "%s", "(anon)");
    }
    pushCString(ret, ">", 1);
    return OBJ_VAL(ret);
}

static Value lxBindingEval(int argCount, Value *args) {
    CHECK_ARITY("Binding#eval", 2, 2, argCount);
    Value self = args[0];
    LxBinding *binding = getBinding(self);
    Value src = args[1];
    char *csrc = VAL_TO_STRING(src)->chars;
    if (strlen(csrc) == 0) {
        return NIL_VAL;
    }
    return VMBindingEval(binding, csrc, "(eval)", 1);
}

void Init_BindingClass(void) {
    lxBindingClass = addGlobalClass("Binding", lxObjClass);
    addNativeMethod(lxBindingClass, "init", lxBindingInit);
    addNativeMethod(lxBindingClass, "localVariables", lxBindingLocalVariables);
    addNativeMethod(lxBindingClass, "localVariableGet", lxBindingLocalVariableGet);
    addNativeMethod(lxBindingClass, "localVariableSet", lxBindingLocalVariableSet);
    addNativeMethod(lxBindingClass, "receiver", lxBindingReceiver);
    addNativeMethod(lxBindingClass, "inspect", lxBindingInspect);
    addNativeMethod(lxBindingClass, "eval", lxBindingEval);
}
