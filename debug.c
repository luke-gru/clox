#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <execinfo.h>
#include <errno.h>
#include <unistd.h>

#include "debug.h"
#include "object.h"
#include "value.h"
#include "memory.h"
#include "compiler.h"
#include "vm.h"

NORETURN void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    int status = 1;
    _exit(status);
}

NORETURN void diePrintCBacktrace(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (vm.inited) {
        fprintf(stderr, "Error in thread: %lld", THREAD() ? (long long)THREAD()->tid : -1);
        if (vm.mainThread == THREAD()) {
            fprintf(stderr, " (main)\n");
        } else {
            fprintf(stderr, "\n");
        }
    } else {
        fprintf(stderr, "VM initialized: NO\n");
    }
    if (THREAD()->lastOp != -1) {
        fprintf(stderr, "Last VM operation: %s\n", opName((OpCode)THREAD()->lastOp));
    }
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    printCBacktrace();
    int status = 1;
    _exit(status);
}

const char *opName(OpCode code) {
    switch (code) {
    case OP_CONSTANT:
        return "OP_CONSTANT";
    case OP_ADD:
        return "OP_ADD";
    case OP_SUBTRACT:
        return "OP_SUBTRACT";
    case OP_MULTIPLY:
        return "OP_MULTIPLY";
    case OP_DIVIDE:
        return "OP_DIVIDE";
    case OP_MODULO:
        return "OP_MODULO";
    case OP_BITOR:
        return "OP_BITOR";
    case OP_BITAND:
        return "OP_BITAND";
    case OP_BITXOR:
        return "OP_BITXOR";
    case OP_SHOVEL_L:
        return "OP_SHOVEL_L";
    case OP_SHOVEL_R:
        return "OP_SHOVEL_R";
    case OP_NEGATE:
        return "OP_NEGATE";
    case OP_NOT:
        return "OP_NOT";
    case OP_LESS:
        return "OP_LESS";
    case OP_GREATER:
        return "OP_GREATER";
    case OP_GREATER_EQUAL:
        return "OP_GREATER_EQUAL";
    case OP_LESS_EQUAL:
        return "OP_LESS_EQUAL";
    case OP_EQUAL:
        return "OP_EQUAL";
    case OP_NOT_EQUAL:
        return "OP_NOT_EQUAL";
    case OP_RETURN:
        return "OP_RETURN";
    case OP_NIL:
        return "OP_NIL";
    case OP_GET_LOCAL:
        return "OP_GET_LOCAL";
    case OP_SET_LOCAL:
        return "OP_SET_LOCAL";
    case OP_UNPACK_SET_LOCAL:
        return "OP_UNPACK_SET_LOCAL";
    case OP_GET_GLOBAL:
        return "OP_GET_GLOBAL";
    case OP_SET_GLOBAL:
        return "OP_SET_GLOBAL";
    case OP_DEFINE_GLOBAL:
        return "OP_DEFINE_GLOBAL";
    case OP_GET_CONST:
        return "OP_GET_CONST";
    case OP_SET_CONST:
        return "OP_SET_CONST";
    case OP_GET_CONST_UNDER:
        return "OP_GET_CONST_UNDER";
    case OP_UNPACK_DEFINE_GLOBAL:
        return "OP_UNPACK_DEFINE_GLOBAL";
    case OP_PROP_GET:
        return "OP_PROP_GET";
    case OP_PROP_SET:
        return "OP_PROP_SET";
    case OP_CLOSURE:
        return "OP_CLOSURE";
    case OP_GET_UPVALUE:
        return "OP_GET_UPVALUE";
    case OP_SET_UPVALUE:
        return "OP_SET_UPVALUE";
    case OP_CLOSE_UPVALUE:
        return "OP_CLOSE_UPVALUE";
    case OP_CALL:
        return "OP_CALL";
    case OP_INVOKE:
        return "OP_INVOKE";
    case OP_STRING:
        return "OP_STRING";
    case OP_ARRAY:
        return "OP_ARRAY";
    case OP_DUPARRAY:
        return "OP_DUPARRAY";
    case OP_MAP:
        return "OP_MAP";
    case OP_DUPMAP:
        return "OP_DUPMAP";
    case OP_REGEX:
        return "OP_REGEX";
    case OP_SPLAT_ARRAY:
        return "OP_SPLAT_ARRAY";
    case OP_GET_THIS:
        return "OP_GET_THIS";
    case OP_GET_SUPER:
        return "OP_GET_SUPER";
    case OP_METHOD:
        return "OP_METHOD";
    case OP_CLASS_METHOD:
        return "OP_CLASS_METHOD";
    case OP_GETTER:
        return "OP_GETTER";
    case OP_SETTER:
        return "OP_SETTER";
    case OP_PRINT:
        return "OP_PRINT";
    case OP_TRUE:
        return "OP_TRUE";
    case OP_FALSE:
        return "OP_FALSE";
    case OP_AND:
        return "OP_AND";
    case OP_OR:
        return "OP_OR";
    case OP_POP:
        return "OP_POP";
    case OP_POP_CREF:
        return "OP_POP_CREF";
    case OP_POP_N:
        return "OP_POP_N";
    case OP_JUMP_IF_FALSE:
        return "OP_JUMP_IF_FALSE";
    case OP_JUMP_IF_TRUE:
        return "OP_JUMP_IF_TRUE";
    case OP_JUMP_IF_FALSE_PEEK:
        return "OP_JUMP_IF_FALSE_P";
    case OP_JUMP_IF_TRUE_PEEK:
        return "OP_JUMP_IF_TRUE_P";
    case OP_JUMP:
        return "OP_JUMP";
    case OP_LOOP:
        return "OP_LOOP";
    case OP_CLASS:
        return "OP_CLASS";
    case OP_MODULE:
        return "OP_MODULE";
    case OP_SUBCLASS:
        return "OP_SUBCLASS";
    case OP_IN:
        return "OP_IN";
    case OP_THROW:
        return "OP_THROW";
    case OP_GET_THROWN:
        return "OP_GET_THROWN";
    case OP_RETHROW_IF_ERR:
        return "OP_RETHROW_IF_ERR";
    case OP_INDEX_GET:
        return "OP_INDEX_GET";
    case OP_INDEX_SET:
        return "OP_INDEX_SET";
    case OP_CHECK_KEYWORD:
        return "OP_CHECK_KEYWORD";
    case OP_ITER:
        return "OP_ITER";
    case OP_ITER_NEXT:
        return "OP_ITER_NEXT";
    case OP_BLOCK_BREAK:
        return "OP_BLOCK_BREAK";
    case OP_BLOCK_CONTINUE:
        return "OP_BLOCK_CONTINUE";
    case OP_BLOCK_RETURN:
        return "OP_BLOCK_RETURN";
    case OP_TO_BLOCK:
        return "OP_TO_BLOCK";
    case OP_LEAVE:
        return "OP_LEAVE";
    default:
        fprintf(stderr, "[BUG]: unknown (unprintable) opcode, maybe new? (%d)\n", code);
        return "!Unknown instruction!";
    }
}

static void addFunc(vec_funcp_t *funcs, ObjFunction *func) {
    if (funcs == NULL) return;
    bool contained = false;
    ObjFunction *fn = NULL; int i = 0;
    vec_foreach(funcs, fn, i) {
        if (fn == func) {
            contained = true;
            break;
        }
    }
    if (!contained) {
        vec_push(funcs, func);
    }
}

static void printCatchTbl(CatchTable *tbl) {
    CatchTable *row = tbl;
    printf("-- catch table --\n");
    int idx = 0;
    while (row) {
        if (row->isEnsure) {
            printf("%d) from: %d, to: %d, target: %d (ensure)\n",
                    idx, row->ifrom, row->ito, row->itarget);
        } else {
            ASSERT(IS_STRING(row->catchVal));
            char *valstr = AS_CSTRING(row->catchVal);
            printf("%d) from: %d, to: %d, target: %d, value: %s\n",
                    idx, row->ifrom, row->ito, row->itarget, valstr);
        }
        row = row->next;
        idx++;
    }
    printf("-- /catch table --\n");
}

static void disassembleCatchTbl(ObjString *buf, CatchTable *tbl) {
    CatchTable *row = tbl;
    pushCString(buf, "-- catch table --\n", 18);
    int idx = 0;
    while (row) {
        ASSERT(IS_STRING(row->catchVal));
        char *valstr = AS_CSTRING(row->catchVal);
        char *cbuf = (char*)calloc(1, strlen(valstr)+1+50);
        ASSERT_MEM(cbuf);
        sprintf(cbuf, "%04d) from: %04d, to: %04d, target: %04d, value: %s\n",
                idx, row->ifrom, row->ito, row->itarget, valstr);
        pushCString(buf, cbuf, strlen(cbuf));
        xfree(cbuf);
        row = row->next;
        idx++;
    }
    pushCString(buf, "-- /catch table --\n", 19);
}

void printFunctionTables(FILE *f, ObjFunction *func) {
  fprintf(f, "--local table--\n");
  LocalVariable *var; int idx = 0;
  vec_foreach(&func->variables, var, idx) {
      ASSERT(var->name);
      char *name = var->name->chars;
      ASSERT(var->scope);
      const char *scope_name = compileScopeName(var->scope->type);
      int slot = var->slot;
      fprintf(f, "%s: %d (%s [%d-%d])\n", name, slot, scope_name,
          var->bytecode_declare_start, var->scope->bytecode_end);
  }
  fprintf(f, "-/local table--\n");
}

/**
 * Print all operations and operands to the console.
 */
void printDisassembledChunk(FILE *f, Chunk *chunk, const char *name) {
    fprintf(f, "== %s ==\n", name);
    vec_funcp_t funcs;
    vec_init(&funcs);

    if (chunk->catchTbl) {
        printCatchTbl(chunk->catchTbl);
    }

    for (int i = 0; i < chunk->count;) {
        i = printDisassembledInstruction(f, chunk, i, &funcs);
        if (i <= 0) {
            break;
        }
    }
    ObjFunction *func = NULL; int i = 0;
    vec_foreach(&funcs, func, i) {
        char *name = func->name ? func->name->chars : (char*)"(anon)";
        fprintf(f, "-- Function %s --\n", name);
        printFunctionTables(f, func);
        printDisassembledChunk(f, func->chunk, name);
        fprintf(f, "----\n");
    }
    vec_deinit(&funcs);
    fprintf(f, "== /%s ==\n", name);
}

static int printConstantInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    bytecode_t constantIdx = chunk->code[i + 1];
    fprintf(f, "%-16s %4" PRId8 " '", op, constantIdx);
    Value constant = getConstant(chunk, constantIdx);
    printValue(f, constant,  false, -1);
    fprintf(f, "'\n");
    return i+2;
}
// instruction has 1 operand, a constant slot index
static int constantInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    bytecode_t constantIdx = chunk->code[i + 1];

    Value constant = getConstant(chunk, constantIdx);
    ObjString *constantStr = valueToString(constant, copyString, NEWOBJ_FLAG_NONE);
    char *constantCStr = constantStr->chars;

    char *cbuf = calloc(1, strlen(op)+1+strlen(constantCStr)+9);
    ASSERT_MEM(cbuf);
    sprintf(cbuf, "%s\t%04" PRId8 "\t'%s'\n", op, constantIdx, constantCStr);

    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+2;
}

static int printStringInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    bytecode_t constantIdx = chunk->code[i + 1];
    bytecode_t isStatic = chunk->code[i + 2];
    fprintf(f, "%-16s %04d '", op, constantIdx);
    Value constant = getConstant(chunk, constantIdx);
    printValue(f, constant,  false, -1);
    fprintf(f, "' (static=%u)\n", isStatic);
    return i+3;
}

static int stringInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    bytecode_t constantIdx = chunk->code[i + 1];
    bytecode_t isStatic = chunk->code[i + 2];
    Value constant = getConstant(chunk, constantIdx);
    ObjString *constantStr = AS_STRING(constant);
    char *constantCStr = constantStr->chars;
    char *cbuf = (char*)calloc(1, strlen(op)+1+strlen(constantCStr)+20);
    sprintf(cbuf, "%s\t%04d\t'%s' (static=%d)\n", op, constantIdx, constantCStr, isStatic);
    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+3;
}

static int printArrayInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    bytecode_t keyValLen = chunk->code[i + 1];
    fprintf(f, "%-16s    len=%03d\n", op, keyValLen);
    return i+2;
}

static int printDupArrayInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    bytecode_t constantIdx = chunk->code[i + 1];
    fprintf(f, "%-16s    ", op);
    Value constant = getConstant(chunk, constantIdx);
    printValue(f, constant,  false, -1);
    fprintf(f, "\n");
    return i+2;
}

static int arrayInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    // TODO
    return i+2;
}

static int dupArrayInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    // TODO
    return i+2;
}

static int printMapInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    bytecode_t keyValLen = chunk->code[i + 1];
    fprintf(f, "%-16s    len=%03d\n", op, keyValLen);
    return i+2;
}

static int printDupMapInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    // TODO
    return i+2;
}

static int mapInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    // TODO
    return i+2;
}

static int dupMapInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    // TODO
    return i+2;
}

static int printLocalVarInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    bytecode_t slotIdx = chunk->code[i + 1];
    bytecode_t varNameIdx = chunk->code[i + 2];
    Value varName = getConstant(chunk, varNameIdx);
    fprintf(f, "%-16s    '%s' [slot %" PRId8 "]\n", op, VAL_TO_STRING(varName)->chars, slotIdx);
    return i+3;
}

static int printUnpackSetVarInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    bytecode_t slotIdx = chunk->code[i + 1];
    bytecode_t unpackIdx = chunk->code[i + 2];
    bytecode_t varNameIdx = chunk->code[i + 3];
    Value varName = getConstant(chunk, varNameIdx);
    fprintf(f, "%-16s    '%s' [slot %d] %d\n", op, VAL_TO_STRING(varName)->chars, slotIdx, unpackIdx);
    return i+4;
}

static int unpackSetVarInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    // TODO
    return i+3;
}

static int printUnpackDefGlobalInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    bytecode_t constantIdx = chunk->code[i + 1];
    Value constant = getConstant(chunk, constantIdx);
    bytecode_t unpackIdx = chunk->code[i + 2];
    fprintf(f, "%-16s    '%s' %d\n", op, AS_STRING(constant)->chars, unpackIdx);
    return i+3;
}

static int unpackDefGlobalInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    // TODO
    return i+3;
}

static int printClosureInstruction(FILE *f, const char *op, Chunk *chunk, int i, vec_funcp_t *funcs) {
    bytecode_t funcConstIdx = chunk->code[i + 1];
    Value constant = getConstant(chunk, funcConstIdx);
    ASSERT(IS_FUNCTION(constant));
    int numUpvalues = AS_FUNCTION(constant)->upvalueCount;

    addFunc(funcs, AS_FUNCTION(constant));

    fprintf(f, "%-16s %4" PRId8 " '", op, funcConstIdx);
    printValue(f, constant, false, -1);
    fprintf(f, "' (upvals: %d)\n", numUpvalues);
    return i+2+(numUpvalues*2);
}

static int closureInstruction(ObjString *buf, const char *op, Chunk *chunk, int i, vec_funcp_t *funcs) {
    bytecode_t funcConstIdx = chunk->code[i + 1];
    Value constant = getConstant(chunk, funcConstIdx);
    ASSERT(IS_FUNCTION(constant));
    int numUpvalues = AS_FUNCTION(constant)->upvalueCount;

    addFunc(funcs, AS_FUNCTION(constant));

    ObjString *constantStr = valueToString(constant, copyString, NEWOBJ_FLAG_NONE);
    char *constantCStr = constantStr->chars;
    char *cbuf = (char*)calloc(1, strlen(op)+1+strlen(constantCStr)+23);
    ASSERT_MEM(cbuf);
    sprintf(cbuf, "%s\t%04" PRId8 "\t'%s'\t(upvals: %03d)\n", op, funcConstIdx,
        constantCStr, numUpvalues);

    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+2+(numUpvalues*2);
}

static int printJumpInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    bytecode_t jumpOffset = chunk->code[i + 1];
    /*ASSERT(jumpOffset != 0); // should have been patched*/
    fprintf(f, "%-16s\t%04d\t(addr=%04d)\n", op, jumpOffset, (i+1+jumpOffset)*4);
    return i+2;
}

static int jumpInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    char *cbuf = (char*)calloc(1, strlen(op)+1+18);
    ASSERT_MEM(cbuf);
    bytecode_t jumpOffset = chunk->code[i + 1];
    /*ASSERT(jumpOffset != 0); // should have been patched*/
    sprintf(cbuf, "%s\t%04d\t(addr=%04d)\n", op, jumpOffset, (i+1+jumpOffset));
    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+2;
}

static int printLoopInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    bytecode_t loopOffset = chunk->code[i + 1];
    fprintf(f, "%-16s %4d (addr=%04d)\n", op, loopOffset, (i*4-(loopOffset*4)));
    return i+2;
}

static int loopInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    char *cbuf = (char*)calloc(1, strlen(op)+1+18);
    ASSERT_MEM(cbuf);
    bytecode_t loopOffset = chunk->code[i + 1];
    sprintf(cbuf, "%s\t%4d\t(addr=%04d)\n", op, loopOffset, (i-loopOffset));
    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+2;
}

static int printCallInstruction(FILE *f, const char *op, Chunk *chunk, int i, vec_funcp_t *funcs) {
    bytecode_t numArgs = chunk->code[i + 1];
    (void)numArgs; // unused
    bytecode_t constantSlot = chunk->code[i + 2];
    Value callInfoVal = getConstant(chunk, constantSlot);
    /*fprintf(f, "typeof=%s\n", typeOfVal(callInfoVal));*/
    ASSERT(IS_INTERNAL(callInfoVal));
    ObjInternal *obj = AS_INTERNAL(callInfoVal);
    CallInfo *callInfo = internalGetData(obj);
    ASSERT(callInfo);
    if (callInfo->blockFunction) {
        addFunc(funcs, callInfo->blockFunction);
    }
    char *callName = tokStr(&callInfo->nameTok);
    if (strcmp(callName, "}") == 0) { // when `fun() { ... }(args)`
        callName = "(anon)";
    }
    fprintf(f, "%-16s    (name=%s, argc=%d, kwargs=%d, splat=%d)\n",
        op, callName, callInfo->argc,
        callInfo->numKwargs, callInfo->usesSplat ? 1 : 0
    );
    return i+3;
}

// TODO: make it like printCallInstruction (show callInfo)
static int callInstruction(ObjString *buf, const char *op, Chunk *chunk, int i, vec_funcp_t *funcs) {
    char *cbuf = calloc(1, strlen(op)+1+11);
    ASSERT_MEM(cbuf);
    bytecode_t numArgs = chunk->code[i + 1];
    bytecode_t callInfoSlot = chunk->code[i + 2];
    Value callInfoVal = getConstant(chunk, callInfoSlot);
    /*fprintf(f, "typeof=%s\n", typeOfVal(callInfoVal));*/
    ASSERT(IS_INTERNAL(callInfoVal));
    ObjInternal *obj = AS_INTERNAL(callInfoVal);
    CallInfo *callInfo = internalGetData(obj);
    ASSERT(callInfo);
    if (callInfo->blockFunction) {
        addFunc(funcs, callInfo->blockFunction);
    }
    sprintf(cbuf, "%s\t(argc=%02d)\n", op, numArgs);
    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+3;
}

// TODO: show callInfo
static int printInvokeInstruction(FILE *f, const char *op, Chunk *chunk, int i, vec_funcp_t *funcs) {
    bytecode_t methodNameArg = chunk->code[i + 1];
    bytecode_t callInfoSlot = chunk->code[i + 3];
    Value callInfoVal = getConstant(chunk, callInfoSlot);
    /*fprintf(f, "typeof=%s\n", typeOfVal(callInfoVal));*/
    ASSERT(IS_INTERNAL(callInfoVal));
    ObjInternal *obj = AS_INTERNAL(callInfoVal);
    CallInfo *callInfo = internalGetData(obj);
    ASSERT(callInfo);
    if (callInfo->blockFunction) {
        addFunc(funcs, callInfo->blockFunction);
    }
    Value methodName = getConstant(chunk, methodNameArg);
    char *methodNameStr = AS_CSTRING(methodName);
    bytecode_t numArgs = chunk->code[i+2];
    fprintf(f, "%-16s    ('%s', argc=%04d)\n", op, methodNameStr, numArgs);
    return i+4;
}

// TODO: show callInfo
static int invokeInstruction(ObjString *buf, const char *op, Chunk *chunk, int i, vec_funcp_t *funcs) {
    bytecode_t methodNameArg = chunk->code[i + 1];
    bytecode_t callInfoSlot = chunk->code[i + 3];
    Value callInfoVal = getConstant(chunk, callInfoSlot);
    /*fprintf(f, "typeof=%s\n", typeOfVal(callInfoVal));*/
    ASSERT(IS_INTERNAL(callInfoVal));
    ObjInternal *obj = AS_INTERNAL(callInfoVal);
    CallInfo *callInfo = internalGetData(obj);
    ASSERT(callInfo);
    if (callInfo->blockFunction) {
        addFunc(funcs, callInfo->blockFunction);
    }
    Value methodName = getConstant(chunk, methodNameArg);
    char *methodNameStr = AS_CSTRING(methodName);
    bytecode_t numArgs = chunk->code[i+2];
    char *cbuf = calloc(1, strlen(op)+1+strlen(methodNameStr)+17);
    ASSERT_MEM(cbuf);
    sprintf(cbuf, "%s\t('%s', argc=%04d)\n", op, methodNameStr, numArgs);
    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+4;
}

static int printCheckKeywordInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    bytecode_t kwargSlot = chunk->code[i+1];
    bytecode_t kwargMapSlot = chunk->code[i+2];
    fprintf(f, "%-16s    kwslot=%d mapslot=%d\n", op, kwargSlot, kwargMapSlot);
    return i+3;
}

static int checkKeywordInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    /*bytecode_t kwargMapSlot = chunk->code[i + 1];*/
    /*bytecode_t kwargSlot = chunk->code[i+2];*/
    // FIXME:
    return i+3;
}

static int localVarInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    bytecode_t slotIdx = chunk->code[i + 1];
    bytecode_t varNameIdx = chunk->code[i + 2];
    Value varName = getConstant(chunk, varNameIdx);
    char *cbuf = calloc(1, strlen(op)+1+12);
    ASSERT_MEM(cbuf);
    sprintf(cbuf, "%s\t'%s' [slot %03d]\n", op, VAL_TO_STRING(varName)->chars, slotIdx);
    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+3;
}

// instruction has no operands
static int printSimpleInstruction(FILE *f, const char *op, int i) {
    fprintf(f, "%s\n", op);
    return i+1;
}

static int printByteInstruction(FILE *f, const char *op, Chunk *chunk, int i) {
    bytecode_t byte = chunk->code[i+1];
    fprintf(f, "%s\t%d\n", op, byte);
    return i+2;
}

// instruction has no operands
static int simpleInstruction(ObjString *buf, const char *op, int i) {
    pushCString(buf, op, strlen(op));
    pushCString(buf, "\n", 1);
    return i+1;
}

static int byteInstruction(ObjString *buf, const char *op, Chunk *chunk, int i) {
    pushCString(buf, op, strlen(op));
    pushCString(buf, "\n", 1);
    // TODO
    return i+2;
}

int printDisassembledInstruction(FILE *f, Chunk *chunk, int i, vec_funcp_t *funcs) {
    fprintf(f, "%04d ", i*BYTES_IN_INSTRUCTION);
    // same line as prev instruction
    if (i > 0 && chunk->lines[i] == chunk->lines[i - 1]) {
        fprintf(f, "   | ");
    } else { // new line
        fprintf(f, "%4d ", chunk->lines[i]);
    }
    bytecode_t byte = chunk->code[i];
    switch (byte) {
        case OP_CONSTANT:
        case OP_DEFINE_GLOBAL:
        case OP_GET_GLOBAL:
        case OP_SET_GLOBAL:
        case OP_GET_CONST:
        case OP_SET_CONST:
        case OP_GET_CONST_UNDER:
        case OP_CLASS:
        case OP_MODULE:
        case OP_SUBCLASS:
        case OP_METHOD:
        case OP_CLASS_METHOD:
        case OP_GETTER:
        case OP_SETTER:
        case OP_PROP_GET:
        case OP_PROP_SET:
        case OP_GET_THROWN:
        case OP_RETHROW_IF_ERR:
        case OP_GET_SUPER:
        case OP_REGEX:
            return printConstantInstruction(f, opName(byte), chunk, i);
        case OP_STRING:
            return printStringInstruction(f, opName(byte), chunk, i);
        case OP_ARRAY:
            return printArrayInstruction(f, opName(byte), chunk, i);
        case OP_DUPARRAY:
            return printDupArrayInstruction(f, opName(byte), chunk, i);
        case OP_MAP:
            return printMapInstruction(f, opName(byte), chunk, i);
        case OP_DUPMAP:
            return printDupMapInstruction(f, opName(byte), chunk, i);
        case OP_GET_LOCAL:
        case OP_SET_LOCAL:
        case OP_SET_UPVALUE:
        case OP_GET_UPVALUE:
            return printLocalVarInstruction(f, opName(byte), chunk, i);
        case OP_UNPACK_SET_LOCAL:
            return printUnpackSetVarInstruction(f, opName(byte), chunk, i);
        case OP_UNPACK_DEFINE_GLOBAL:
            return printUnpackDefGlobalInstruction(f, opName(byte), chunk, i);
        case OP_CLOSURE:
            return printClosureInstruction(f, opName(byte), chunk, i, funcs);
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_TRUE:
        case OP_JUMP_IF_FALSE_PEEK:
        case OP_JUMP_IF_TRUE_PEEK:
            return printJumpInstruction(f, opName(byte), chunk, i);
        case OP_LOOP:
            return printLoopInstruction(f, opName(byte), chunk, i);
        case OP_CALL:
            return printCallInstruction(f, opName(byte), chunk, i, funcs);
        case OP_INVOKE:
            return printInvokeInstruction(f, opName(byte), chunk, i, funcs);
        case OP_CHECK_KEYWORD:
            return printCheckKeywordInstruction(f, opName(byte), chunk, i);
        case OP_NEGATE:
        case OP_RETURN:
        case OP_ADD:
        case OP_SUBTRACT:
        case OP_MULTIPLY:
        case OP_DIVIDE:
        case OP_MODULO:
        case OP_BITOR:
        case OP_BITAND:
        case OP_BITXOR:
        case OP_SHOVEL_L:
        case OP_SHOVEL_R:
        case OP_LESS:
        case OP_GREATER:
        case OP_GREATER_EQUAL:
        case OP_LESS_EQUAL:
        case OP_EQUAL:
        case OP_NOT_EQUAL:
        case OP_NOT:
        case OP_PRINT:
        case OP_TRUE:
        case OP_FALSE:
        case OP_NIL:
        case OP_AND:
        case OP_OR:
        case OP_POP:
        case OP_POP_CREF:
        case OP_LEAVE:
        case OP_THROW:
        case OP_INDEX_GET:
        case OP_INDEX_SET:
        case OP_CLOSE_UPVALUE:
        case OP_IN:
        case OP_GET_THIS:
        case OP_SPLAT_ARRAY:
        case OP_ITER:
        case OP_ITER_NEXT:
        case OP_BLOCK_BREAK:
        case OP_BLOCK_CONTINUE:
        case OP_BLOCK_RETURN:
        case OP_TO_BLOCK:
            return printSimpleInstruction(f, opName(byte), i);
        case OP_POP_N:
            return printByteInstruction(f, opName(byte), chunk, i);
        default:
            fprintf(f, "Unknown opcode %" PRId8 " (%s)\n", byte, opName(byte));
            return -1;
    }
}

static int disassembledInstruction(ObjString *buf, Chunk *chunk, int i, vec_funcp_t *funcs) {
    char numBuf[12] = {'\0'};
    sprintf(numBuf, "%04d\t", i*BYTES_IN_INSTRUCTION);
    pushCString(buf, numBuf, strlen(numBuf));
    bytecode_t byte = chunk->code[i];
    switch (byte) {
        case OP_CONSTANT:
        case OP_DEFINE_GLOBAL:
        case OP_GET_GLOBAL:
        case OP_SET_GLOBAL:
        case OP_GET_CONST:
        case OP_SET_CONST:
        case OP_GET_CONST_UNDER:
        case OP_CLASS:
        case OP_MODULE:
        case OP_SUBCLASS:
        case OP_METHOD:
        case OP_CLASS_METHOD:
        case OP_GETTER:
        case OP_SETTER:
        case OP_PROP_GET:
        case OP_PROP_SET:
        case OP_GET_THROWN:
        case OP_RETHROW_IF_ERR:
        case OP_GET_SUPER:
        case OP_REGEX:
            return constantInstruction(buf, opName(byte), chunk, i);
        case OP_STRING:
            return stringInstruction(buf, opName(byte), chunk, i);
        case OP_ARRAY:
            return arrayInstruction(buf, opName(byte), chunk, i);
        case OP_DUPARRAY:
            return dupArrayInstruction(buf, opName(byte), chunk, i);
        case OP_MAP:
            return mapInstruction(buf, opName(byte), chunk, i);
        case OP_DUPMAP:
            return dupMapInstruction(buf, opName(byte), chunk, i);
        case OP_GET_LOCAL:
        case OP_SET_LOCAL:
        case OP_SET_UPVALUE:
        case OP_GET_UPVALUE:
            return localVarInstruction(buf, opName(byte), chunk, i);
        case OP_UNPACK_SET_LOCAL:
            return unpackSetVarInstruction(buf, opName(byte), chunk, i);
        case OP_UNPACK_DEFINE_GLOBAL:
            return unpackDefGlobalInstruction(buf, opName(byte), chunk, i);
        case OP_CLOSURE:
            return closureInstruction(buf, opName(byte), chunk, i, funcs);
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_TRUE:
        case OP_JUMP_IF_FALSE_PEEK:
        case OP_JUMP_IF_TRUE_PEEK:
            return jumpInstruction(buf, opName(byte), chunk, i);
        case OP_LOOP:
            return loopInstruction(buf, opName(byte), chunk, i);
        case OP_CALL:
            return callInstruction(buf, opName(byte), chunk, i, funcs);
        case OP_INVOKE:
            return invokeInstruction(buf, opName(byte), chunk, i, funcs);
        case OP_CHECK_KEYWORD:
            return checkKeywordInstruction(buf, opName(byte), chunk, i);
        case OP_NEGATE:
        case OP_RETURN:
        case OP_ADD:
        case OP_SUBTRACT:
        case OP_MULTIPLY:
        case OP_DIVIDE:
        case OP_MODULO:
        case OP_BITOR:
        case OP_BITAND:
        case OP_BITXOR:
        case OP_SHOVEL_L:
        case OP_SHOVEL_R:
        case OP_LESS:
        case OP_GREATER:
        case OP_GREATER_EQUAL:
        case OP_LESS_EQUAL:
        case OP_EQUAL:
        case OP_NOT_EQUAL:
        case OP_NOT:
        case OP_PRINT:
        case OP_TRUE:
        case OP_FALSE:
        case OP_NIL:
        case OP_AND:
        case OP_OR:
        case OP_POP:
        case OP_POP_CREF:
        case OP_LEAVE:
        case OP_THROW:
        case OP_INDEX_GET:
        case OP_INDEX_SET:
        case OP_CLOSE_UPVALUE:
        case OP_IN:
        case OP_GET_THIS:
        case OP_SPLAT_ARRAY:
        case OP_ITER:
        case OP_ITER_NEXT:
        case OP_BLOCK_BREAK:
        case OP_BLOCK_CONTINUE:
        case OP_BLOCK_RETURN:
        case OP_TO_BLOCK:
            return simpleInstruction(buf, opName(byte), i);
        case OP_POP_N:
            return byteInstruction(buf, opName(byte), chunk, i);
        default: {
            ASSERT(0);
            char *cBuf = calloc(1, 19+1);
            ASSERT_MEM(cBuf);
            sprintf(cBuf, "Unknown opcode %03" PRId8 "\n", byte);
            pushCString(buf, cBuf, strlen(cBuf));
            xfree(cBuf);
            return -1;
        }
    }
}

ObjString *disassembleChunk(Chunk *chunk) {
    vec_funcp_t funcs;
    vec_init(&funcs);

    ObjString *buf = copyString("", 0, NEWOBJ_FLAG_NONE);

    // catch table
    if (chunk->catchTbl) {
        disassembleCatchTbl(buf, chunk->catchTbl);
    }

    for (int i = 0; i < chunk->count;) {
        i = disassembledInstruction(buf, chunk, i, &funcs);
        if (i <= 0) {
            break;
        }
    }

    // inner functions
    ObjFunction *func = NULL; int i = 0;
    vec_foreach(&funcs, func, i) {
        ASSERT(((Obj*)func)->type == OBJ_T_FUNCTION);
        char *name;
        if (func->name) {
            name = func->name->chars;
        } else {
            name = "(anon)";
        }
        char *cbuf = calloc(1, strlen(name)+1+16);
        ASSERT_MEM(cbuf);
        fprintf(stderr, "Function name: '%s'\n", name);
        sprintf(cbuf, "-- Function %s --\n", name);
        pushCString(buf, cbuf, strlen(cbuf));
        ObjString *funcStr = disassembleChunk(func->chunk);
        pushCString(buf, funcStr->chars, strlen(funcStr->chars));
        pushCString(buf, "----\n", strlen("----\n"));
        xfree(cbuf);
    }
    vec_deinit(&funcs);
    return buf;
}

#ifdef NDEBUG
void printCBacktrace(void) { }
#else
static void full_write(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t ret = write(fd, buf, len);

        if ((ret == -1) && (errno != EINTR))
            break;

        buf += (size_t) ret;
        len -= (size_t) ret;
    }
}

void printCBacktrace(void) {
    static const char start[] = "C BACKTRACE ------------\n";
    static const char end[] = "----------------------\n";

    void *bt[1024];
    int bt_size;
    char **bt_syms;
    int i;

    bt_size = backtrace(bt, 1024);
    bt_syms = backtrace_symbols(bt, bt_size);
    full_write(STDERR_FILENO, start, strlen(start));
    for (i = 1; i < bt_size; i++) {
        size_t len = strlen(bt_syms[i]);
        full_write(STDERR_FILENO, bt_syms[i], len);
        full_write(STDERR_FILENO, "\n", 1);
    }
    full_write(STDERR_FILENO, end, strlen(end));
    xfree(bt_syms);
}
#endif
