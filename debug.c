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
    exit(1);
}

NORETURN void diePrintCBacktrace(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    printCBacktrace();
    exit(1);
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
    case OP_MAP:
        return "OP_MAP";
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

void printCatchTbl(CatchTable *tbl) {
    CatchTable *row = tbl;
    printf("-- catch table --\n");
    int idx = 0;
    while (row) {
        ASSERT(IS_STRING(row->catchVal));
        char *valstr = AS_CSTRING(row->catchVal);
        printf("%d) from: %d, to: %d, target: %d, value: %s\n",
                idx, row->ifrom, row->ito, row->itarget, valstr);
        row = row->next;
        idx++;
    }
    printf("-- /catch table --\n");
}

void disassembleCatchTbl(ObjString *buf, CatchTable *tbl) {
    CatchTable *row = tbl;
    pushCString(buf, "-- catch table --\n", 18);
    int idx = 0;
    while (row) {
        ASSERT(IS_STRING(row->catchVal));
        char *valstr = AS_CSTRING(row->catchVal);
        char *cbuf = calloc(strlen(valstr)+1+50, 1);
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
        char *name = func->name ? func->name->chars : "(anon)";
        fprintf(f, "-- Function %s --\n", name);
        printDisassembledChunk(f, &func->chunk, name);
        fprintf(f, "----\n");
    }
    vec_deinit(&funcs);
    fprintf(f, "== /%s ==\n", name);
}

static int printConstantInstruction(FILE *f, char *op, Chunk *chunk, int i) {
    uint8_t constantIdx = chunk->code[i + 1];
    fprintf(f, "%-16s %4" PRId8 " '", op, constantIdx);
    Value constant = getConstant(chunk, constantIdx);
    printValue(f, constant,  false);
    fprintf(f, "'\n");
    return i+2;
}
// instruction has 1 operand, a constant slot index
static int constantInstruction(ObjString *buf, char *op, Chunk *chunk, int i) {
    uint8_t constantIdx = chunk->code[i + 1];

    Value constant = getConstant(chunk, constantIdx);
    ObjString *constantStr = valueToString(constant, copyString);
    char *constantCStr = constantStr->chars;

    char *cbuf = calloc(strlen(op)+1+strlen(constantCStr)+9, 1);
    ASSERT_MEM(cbuf);
    sprintf(cbuf, "%s\t%04" PRId8 "\t'%s'\n", op, constantIdx, constantCStr);

    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+2;
}

static int printStringInstruction(FILE *f, char *op, Chunk *chunk, int i) {
    uint8_t constantIdx = chunk->code[i + 1];
    uint8_t isStatic = chunk->code[i + 2];
    fprintf(f, "%-16s %04d '", op, constantIdx);
    Value constant = getConstant(chunk, constantIdx);
    printValue(f, constant,  false);
    fprintf(f, "' (static=%u)\n", isStatic);
    return i+3;
}

static int stringInstruction(ObjString *buf, char *op, Chunk *chunk, int i) {
    uint8_t constantIdx = chunk->code[i + 1];
    uint8_t isStatic = chunk->code[i + 2];
    Value constant = getConstant(chunk, constantIdx);
    ObjString *constantStr = AS_STRING(constant);
    char *constantCStr = constantStr->chars;
    char *cbuf = calloc(strlen(op)+1+strlen(constantCStr)+20, 1);
    sprintf(cbuf, "%s\t%04d\t'%s' (static=%d)\n", op, constantIdx, constantCStr, isStatic);
    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+3;
}

static int printArrayInstruction(FILE *f, char *op, Chunk *chunk, int i) {
    uint8_t keyValLen = chunk->code[i + 1];
    fprintf(f, "%-16s    len=%03d\n", op, keyValLen);
    return i+2;
}

static int arrayInstruction(ObjString *buf, char *op, Chunk *chunk, int i) {
    // TODO
    return i+2;
}

static int printMapInstruction(FILE *f, char *op, Chunk *chunk, int i) {
    uint8_t keyValLen = chunk->code[i + 1];
    fprintf(f, "%-16s    len=%03d\n", op, keyValLen);
    return i+2;
}

static int mapInstruction(ObjString *buf, char *op, Chunk *chunk, int i) {
    // TODO
    return i+2;
}

static int printLocalVarInstruction(FILE *f, char *op, Chunk *chunk, int i) {
    uint8_t slotIdx = chunk->code[i + 1];
    uint8_t varNameIdx = chunk->code[i + 2];
    Value varName = getConstant(chunk, varNameIdx);
    fprintf(f, "%-16s    '%s' [slot %" PRId8 "]\n", op, VAL_TO_STRING(varName)->chars, slotIdx);
    return i+3;
}

static int printUnpackSetVarInstruction(FILE *f, char *op, Chunk *chunk, int i) {
    uint8_t slotIdx = chunk->code[i + 1];
    uint8_t unpackIdx = chunk->code[i + 2];
    uint8_t varNameIdx = chunk->code[i + 3];
    Value varName = getConstant(chunk, varNameIdx);
    fprintf(f, "%-16s    '%s' [slot %d] %d\n", op, VAL_TO_STRING(varName)->chars, slotIdx, unpackIdx);
    return i+4;
}

static int unpackSetVarInstruction(ObjString *buf, char *op, Chunk *chunk, int i) {
    // TODO
    return i+3;
}

static int printClosureInstruction(FILE *f, char *op, Chunk *chunk, int i, vec_funcp_t *funcs) {
    uint8_t funcConstIdx = chunk->code[i + 1];
    Value constant = getConstant(chunk, funcConstIdx);
    ASSERT(IS_FUNCTION(constant));
    int numUpvalues = AS_FUNCTION(constant)->upvalueCount;

    addFunc(funcs, AS_FUNCTION(constant));

    fprintf(f, "%-16s %4" PRId8 " '", op, funcConstIdx);
    printValue(f, constant, false);
    fprintf(f, "' (upvals: %d)\n", numUpvalues);
    return i+2+(numUpvalues*2);
}

static int closureInstruction(ObjString *buf, char *op, Chunk *chunk, int i, vec_funcp_t *funcs) {
    uint8_t funcConstIdx = chunk->code[i + 1];
    Value constant = getConstant(chunk, funcConstIdx);
    ASSERT(IS_FUNCTION(constant));
    int numUpvalues = AS_FUNCTION(constant)->upvalueCount;

    addFunc(funcs, AS_FUNCTION(constant));

    ObjString *constantStr = valueToString(constant, copyString);
    char *constantCStr = constantStr->chars;
    char *cbuf = calloc(strlen(op)+1+strlen(constantCStr)+23, 1);
    ASSERT_MEM(cbuf);
    sprintf(cbuf, "%s\t%04" PRId8 "\t'%s'\t(upvals: %03d)\n", op, funcConstIdx,
        constantCStr, numUpvalues);

    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+2+(numUpvalues*2);
}

static int printJumpInstruction(FILE *f, char *op, Chunk *chunk, int i) {
    uint8_t jumpOffset = chunk->code[i + 1];
    /*ASSERT(jumpOffset != 0); // should have been patched*/
    fprintf(f, "%-16s\t%04" PRId8 "\t(addr=%04" PRId8 ")\n", op, jumpOffset, (i+1+jumpOffset));
    return i+2;
}

static int jumpInstruction(ObjString *buf, char *op, Chunk *chunk, int i) {
    char *cbuf = calloc(strlen(op)+1+18, 1);
    ASSERT_MEM(cbuf);
    uint8_t jumpOffset = chunk->code[i + 1];
    /*ASSERT(jumpOffset != 0); // should have been patched*/
    sprintf(cbuf, "%s\t%04" PRId8 "\t(addr=%04" PRId8 ")\n", op, jumpOffset, (i+1+jumpOffset));
    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+2;
}

static int printLoopInstruction(FILE *f, char *op, Chunk *chunk, int i) {
    uint8_t loopOffset = chunk->code[i + 1];
    fprintf(f, "%-16s %4" PRId8 " (addr=%04" PRId8 ")\n", op, loopOffset, (i-loopOffset));
    return i+2;
}

static int loopInstruction(ObjString *buf, char *op, Chunk *chunk, int i) {
    char *cbuf = calloc(strlen(op)+1+18, 1);
    ASSERT_MEM(cbuf);
    uint8_t loopOffset = chunk->code[i + 1];
    sprintf(cbuf, "%s\t%4" PRId8 "\t(addr=%04" PRId8 ")\n", op, loopOffset, (i-loopOffset));
    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+2;
}

static int printCallInstruction(FILE *f, char *op, Chunk *chunk, int i) {
    uint8_t numArgs = chunk->code[i + 1];
    (void)numArgs; // unused
    uint8_t constantSlot = chunk->code[i + 2];
    Value callInfoVal = getConstant(chunk, constantSlot);
    /*fprintf(f, "typeof=%s\n", typeOfVal(callInfoVal));*/
    ASSERT(IS_INTERNAL(callInfoVal));
    ObjInternal *obj = AS_INTERNAL(callInfoVal);
    CallInfo *callInfo = internalGetData(obj);
    ASSERT(callInfo);
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
static int callInstruction(ObjString *buf, char *op, Chunk *chunk, int i) {
    char *cbuf = calloc(strlen(op)+1+11, 1);
    ASSERT_MEM(cbuf);
    uint8_t numArgs = chunk->code[i + 1];
    sprintf(cbuf, "%s\t(argc=%02d)\n", op, numArgs);
    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+3;
}

// TODO: show callInfo
static int printInvokeInstruction(FILE *f, char *op, Chunk *chunk, int i) {
    uint8_t methodNameArg = chunk->code[i + 1];
    Value methodName = getConstant(chunk, methodNameArg);
    char *methodNameStr = AS_CSTRING(methodName);
    uint8_t numArgs = chunk->code[i+2];
    fprintf(f, "%-16s    ('%s', argc=%04" PRId8 ")\n", op, methodNameStr, numArgs);
    return i+4;
}

// TODO: show callInfo
static int invokeInstruction(ObjString *buf, char *op, Chunk *chunk, int i) {
    uint8_t methodNameArg = chunk->code[i + 1];
    Value methodName = getConstant(chunk, methodNameArg);
    char *methodNameStr = AS_CSTRING(methodName);
    uint8_t numArgs = chunk->code[i+2];
    char *cbuf = calloc(strlen(op)+1+strlen(methodNameStr)+17, 1);
    ASSERT_MEM(cbuf);
    sprintf(cbuf, "%s\t('%s', argc=%04" PRId8 ")\n", op, methodNameStr, numArgs);
    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+4;
}

static int printCheckKeywordInstruction(FILE *f, char *op, Chunk *chunk, int i) {
    uint8_t kwargSlot = chunk->code[i+1];
    uint8_t kwargMapSlot = chunk->code[i+2];
    fprintf(f, "%-16s    kwslot=%d mapslot=%d\n", op, kwargSlot, kwargMapSlot);
    return i+3;
}

static int checkKeywordInstruction(ObjString *buf, char *op, Chunk *chunk, int i) {
    /*uint8_t kwargMapSlot = chunk->code[i + 1];*/
    /*uint8_t kwargSlot = chunk->code[i+2];*/
    // FIXME:
    return i+3;
}

static int localVarInstruction(ObjString *buf, char *op, Chunk *chunk, int i) {
    uint8_t slotIdx = chunk->code[i + 1];
    uint8_t varNameIdx = chunk->code[i + 2];
    Value varName = getConstant(chunk, varNameIdx);
    char *cbuf = calloc(strlen(op)+1+12, 1);
    ASSERT_MEM(cbuf);
    sprintf(cbuf, "%s\t'%s' [slot %03" PRId8 "]\n", op, VAL_TO_STRING(varName)->chars, slotIdx);
    pushCString(buf, cbuf, strlen(cbuf));
    xfree(cbuf);
    return i+3;
}

// instruction has no operands
static int printSimpleInstruction(FILE *f, char *op, int i) {
    fprintf(f, "%s\n", op);
    return i+1;
}

// instruction has no operands
static int simpleInstruction(ObjString *buf, char *op, int i) {
    pushCString(buf, op, strlen(op));
    pushCString(buf, "\n", 1);
    return i+1;
}

int printDisassembledInstruction(FILE *f, Chunk *chunk, int i, vec_funcp_t *funcs) {
    fprintf(f, "%04d ", i);
    // same line as prev instruction
    if (i > 0 && chunk->lines[i] == chunk->lines[i - 1]) {
        fprintf(f, "   | ");
    } else { // new line
        fprintf(f, "%4d ", chunk->lines[i]);
    }
    uint8_t byte = chunk->code[i];
    switch (byte) {
        case OP_CONSTANT:
        case OP_DEFINE_GLOBAL:
        case OP_GET_GLOBAL:
        case OP_SET_GLOBAL:
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
        case OP_GET_SUPER:
            return printConstantInstruction(f, opName(byte), chunk, i);
        case OP_STRING:
            return printStringInstruction(f, opName(byte), chunk, i);
        case OP_ARRAY:
            return printArrayInstruction(f, opName(byte), chunk, i);
        case OP_MAP:
            return printMapInstruction(f, opName(byte), chunk, i);
        case OP_GET_LOCAL:
        case OP_SET_LOCAL:
        case OP_SET_UPVALUE:
        case OP_GET_UPVALUE:
            return printLocalVarInstruction(f, opName(byte), chunk, i);
        case OP_UNPACK_SET_LOCAL:
            return printUnpackSetVarInstruction(f, opName(byte), chunk, i);
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
            return printCallInstruction(f, opName(byte), chunk, i);
        case OP_INVOKE:
            return printInvokeInstruction(f, opName(byte), chunk, i);
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
        case OP_PRINT:
        case OP_TRUE:
        case OP_FALSE:
        case OP_NIL:
        case OP_AND:
        case OP_OR:
        case OP_POP:
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
            return printSimpleInstruction(f, opName(byte), i);
        default:
            fprintf(f, "Unknown opcode %" PRId8 " (%s)\n", byte, opName(byte));
            return -1;
    }
}

static int disassembledInstruction(ObjString *buf, Chunk *chunk, int i, vec_funcp_t *funcs) {
    char *numBuf = calloc(5+1, 1);
    ASSERT_MEM(numBuf);
    sprintf(numBuf, "%04d\t", i);
    pushCString(buf, numBuf, strlen(numBuf));
    xfree(numBuf);
    uint8_t byte = chunk->code[i];
    switch (byte) {
        case OP_CONSTANT:
        case OP_DEFINE_GLOBAL:
        case OP_GET_GLOBAL:
        case OP_SET_GLOBAL:
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
        case OP_GET_SUPER:
            return constantInstruction(buf, opName(byte), chunk, i);
        case OP_STRING:
            return stringInstruction(buf, opName(byte), chunk, i);
        case OP_ARRAY:
            return arrayInstruction(buf, opName(byte), chunk, i);
        case OP_MAP:
            return mapInstruction(buf, opName(byte), chunk, i);
        case OP_GET_LOCAL:
        case OP_SET_LOCAL:
        case OP_SET_UPVALUE:
        case OP_GET_UPVALUE:
            return localVarInstruction(buf, opName(byte), chunk, i);
        case OP_UNPACK_SET_LOCAL:
            return unpackSetVarInstruction(buf, opName(byte), chunk, i);
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
            return callInstruction(buf, opName(byte), chunk, i);
        case OP_INVOKE:
            return invokeInstruction(buf, opName(byte), chunk, i);
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
        case OP_PRINT:
        case OP_TRUE:
        case OP_FALSE:
        case OP_NIL:
        case OP_AND:
        case OP_OR:
        case OP_POP:
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
            return simpleInstruction(buf, opName(byte), i);
        default: {
            ASSERT(0);
            char *cBuf = calloc(19+1, 1);
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

    ObjString *buf = copyString("", 0);

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
        char *cbuf = calloc(strlen(name)+1+16, 1);
        ASSERT_MEM(cbuf);
        fprintf(stderr, "Function name: '%s'\n", name);
        sprintf(cbuf, "-- Function %s --\n", name);
        pushCString(buf, cbuf, strlen(cbuf));
        ObjString *funcStr = disassembleChunk(&func->chunk);
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
