#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stddef.h>
#include "compiler.h"
#include "nodes.h"
#include "scanner.h"
#include "parser.h"
#include "value.h"
#include "debug.h"
#include "options.h"
#include "memory.h"
#include "vm.h"

#ifdef NDEBUG
#define COMP_TRACE(...) (void)0
#else
#define COMP_TRACE(...) compiler_trace_debug(__VA_ARGS__)
#endif

const char *compileScopeName(CompileScopeType stype) {
    switch (stype) {
    case COMPILE_SCOPE_MAIN: return "SCOPE_MAIN"; // in a { } block, NOT language-level function 'block'
    case COMPILE_SCOPE_FUNCTION: return "SCOPE_FUNCTION";
    case COMPILE_SCOPE_IF: return "SCOPE_IF";
    case COMPILE_SCOPE_WHILE: return "SCOPE_WHILE";
    case COMPILE_SCOPE_FOREACH: return "SCOPE_FOREACH";
    case COMPILE_SCOPE_FOR: return "SCOPE_FOR";
    case COMPILE_SCOPE_TRY: return "SCOPE_TRY";
    case COMPILE_SCOPE_IN: return "SCOPE_IN";
    case COMPILE_SCOPE_CLASS: return "SCOPE_CLASS";
    case COMPILE_SCOPE_MODULE: return "SCOPE_MODULE";
    case COMPILE_SCOPE_BLOCK: return "SCOPE_BLOCK";
    default: {
        UNREACHABLE("invalid scope type: %d", stype);
    }
    }
}

typedef enum eLoopType {
  LOOP_T_NONE=0,
  LOOP_T_FOR,
  LOOP_T_FOREACH,
  LOOP_T_WHILE,
  LOOP_T_BLOCK,
} eLoopType;

static Compiler *current = NULL;
static Compiler *top = NULL; // compiler for main
static ClassCompiler *currentClassOrModule = NULL;
static bool inINBlock = false;
static Token *curTok = NULL;
static int loopStart = -1;
static eLoopType curLoopType = LOOP_T_NONE;
static int nodeDepth = 0;
static int nodeWidth = -1;
static int blockDepth = 0;
static bool breakBlock = false;
static Scope *curScope = NULL;
static int loopLocalCount; // for continue statement
static int nextInsnIsLabel = false;

CompilerOpts compilerOpts; // [external]

typedef enum {
    VAR_GET = 1,
    VAR_SET,
} VarOp;

typedef enum {
    CONST_T_NUMLIT = 1,
    CONST_T_STRLIT,
    CONST_T_ARYLIT,
    CONST_T_MAPLIT,
    CONST_T_CODE,
    CONST_T_CALLINFO
} ConstType;

static void compiler_trace_debug(const char *fmt, ...) {
    if (!CLOX_OPTION_T(traceCompiler)) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[COMP]: ");
    if (current) {
        fprintf(stderr, "(comp=%p,depth=%d): ", current, current->scopeDepth);
    }
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static void optimizer_debug(int lvl, const char *fmt, ...) {
    if (GET_OPTION(debugOptimizerLvl) < lvl) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[OPT]: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

#ifdef NDEBUG
#define OPT_DEBUG(lvl, ...) (void)0
#else
#define OPT_DEBUG(lvl, ...) optimizer_debug(lvl, __VA_ARGS__)
#endif

static void error(const char *format, ...) {
    int line = curTok ? curTok->line : 0;
    va_list args;
    va_start(args, format);
    ObjString *str = hiddenString("", 0, NEWOBJ_FLAG_NONE);
    pushCStringFmt(str, "[Compile Error]: ");
    if (line > 0) {
        pushCStringFmt(str, "(line: %d) ", line);
    }
    pushCStringVFmt(str, format, args);
    va_end(args);
    pushCStringFmt(str, "%s", "\n");
    vec_push(&top->v_errMessages, str);

    current->hadError = true;
    top->hadError = true;
}

static void outputCompilerErrors(Compiler *c, FILE *f) {
    ASSERT(top);
    ObjString *msg = NULL;
    int i = 0;
    vec_foreach(&top->v_errMessages, msg, i) {
        fprintf(f, "%s", msg->chars);
    }
}

static void freeCompiler(Compiler *c, bool freeErrMessages) {
    if (freeErrMessages) {
        vec_deinit(&c->v_errMessages);
    }
    freeTable(&c->constTbl);
    freeIseq(&c->iseq);
}

static inline Chunk *currentChunk() {
    DBG_ASSERT(current->function && current->function->chunk);
    return current->function->chunk;
}

static inline ObjFunction *currentFunction() {
  return current->function;
}

static inline Iseq *currentIseq() {
    return &current->iseq;
}

static Insn *emitInsn(Insn in) {
    COMP_TRACE("emitInsn: %s", opName(in.code));
    in.lineno = curTok ? curTok->line : 0;
    in.nlvl.depth = nodeDepth;
    in.nlvl.width = nodeWidth;
    Insn *inHeap = xcalloc(1, sizeof(Insn));
    ASSERT_MEM(inHeap);
    memcpy(inHeap, &in, sizeof(Insn));
    if (nextInsnIsLabel) {
        inHeap->isLabel = true;
        nextInsnIsLabel = false;
    }
    iseqAddInsn(currentIseq(), inHeap);
    return inHeap;
}

static Insn *emitOp0(bytecode_t code) {
    Insn in;
    memset(&in, 0, sizeof(Insn));
    in.code = code;
    in.numOperands = 0;
    return emitInsn(in);
}
static Insn *emitOp1(bytecode_t code, bytecode_t op1) {
    Insn in;
    memset(&in, 0, sizeof(Insn));
    in.code = code;
    in.operands[0] = op1;
    in.numOperands = 1;
    return emitInsn(in);
}
static Insn *emitOp2(bytecode_t code, bytecode_t op1, bytecode_t op2) {
    Insn in;
    memset(&in, 0, sizeof(Insn));
    in.code = code;
    in.operands[0] = op1;
    in.operands[1] = op2;
    in.numOperands = 2;
    return emitInsn(in);
}
static Insn *emitOp3(bytecode_t code, bytecode_t op1, bytecode_t op2, bytecode_t op3) {
    Insn in;
    memset(&in, 0, sizeof(Insn));
    in.code = code;
    in.operands[0] = op1;
    in.operands[1] = op2;
    in.operands[2] = op3;
    in.numOperands = 3;
    return emitInsn(in);
}

// blocks (`{}`) push new scopes
static void pushScope(CompileScopeType stype) {
    ObjFunction *func = current->function;
    Scope *s = ALLOCATE(Scope, 1);
    s->type = stype;
    s->line_start = curTok ? curTok->line : 0;
    s->line_end = -1;
    s->parent = curScope;
    s->bytecode_start = current->iseq.wordCount;
    s->bytecode_end = -1;
    vec_push(&func->scopes, s);
    current->scopeDepth++;
    curScope = s;
    COMP_TRACE("pushScope: %s (depth=%d)", compileScopeName(stype), current->scopeDepth);
}

static void namedVariable(Token name, VarOp getSet);

// returns nil, this is in case OP_RETURN wasn't emitted from an explicit
// `return` statement in a function.
static void emitReturn(Compiler *compiler) {
    ASSERT(compiler->type != FUN_TYPE_TOP_LEVEL);
    COMP_TRACE("Emitting return");
    if (compiler->type == FUN_TYPE_INIT) {
        emitOp0(OP_GET_THIS);
        emitOp0(OP_RETURN);
    } else {
        if (breakBlock && compiler->type == FUN_TYPE_BLOCK) {
            if (compiler->iseq.count == 0) {
                emitOp0(OP_NIL);
            }
            emitOp0(OP_BLOCK_CONTINUE); // continue with last evaluated statement
        } else if (compiler->type == FUN_TYPE_BLOCK) {
            if (compiler->iseq.count == 0) {
                emitOp0(OP_NIL);
                emitOp0(OP_POP);
            }
            emitOp0(OP_BLOCK_CONTINUE); // continue with last evaluated statement
        } else {
            emitOp0(OP_NIL);
            emitOp0(OP_RETURN);
        }
    }
}

static void emitCloseUpvalue(void) {
    COMP_TRACE("Emitting close upvalue");
    emitOp0(OP_CLOSE_UPVALUE);
}

static void popScope(CompileScopeType stype) {
    COMP_TRACE("popScope: %s (depth=%d)", compileScopeName(stype), current->scopeDepth);
    while (current->localCount > 0 && current->locals[current->localCount - 1].depth >= current->scopeDepth) {
        Local *local = &current->locals[current->localCount-1];
        if (local->isUpvalue) {
            COMP_TRACE("popScope closing upvalue");
            emitCloseUpvalue();
        } else {
            if (current->type != FUN_TYPE_BLOCK && local->popOnScopeEnd) {
                COMP_TRACE("popScope emitting OP_POP");
                emitOp0(OP_POP);
            }
        }
        current->localCount--;
    }
    if (stype == COMPILE_SCOPE_FUNCTION || stype == COMPILE_SCOPE_MAIN) {
        emitReturn(current);
    }
    curScope->bytecode_end = currentIseq()->wordCount;
    curScope->line_end = curTok ? curTok->line : 0;
    curScope = curScope->parent;
    current->scopeDepth--;
}

static inline void emitNil() {
    emitOp0(OP_NIL);
}

// exit from script
static inline void emitLeave() {
    emitOp0(OP_LEAVE);
}

// Adds an upvalue to [compiler]'s function with the given properties. Does not
// add one if an upvalue for that variable is already in the list. Returns the
// index of the upvalue.
static int addUpvalue(Compiler *compiler, uint8_t index, bool isLocal) {
    // Look for an existing one.
    COMP_TRACE("Adding upvalue to COMP=%p, index: %d, isLocal: %s",
            compiler, index, isLocal ? "true" : "false");
    for (int i = 0; i < compiler->function->upvalueCount; i++) {
        Upvalue *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) return i;
    }

    if (compiler->function->upvalueCount == LX_MAX_UPVALUES) {
        error("Too many closure variables in function.");
        return 0;
    }

    // If we got here, it's a new upvalue.
    compiler->upvalues[compiler->function->upvalueCount].isLocal = isLocal;
    compiler->upvalues[compiler->function->upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static bool identifiersEqual(Token *a, Token *b) {
    if (a->length != b->length) return false;
    return memcmp(tokStr(a), tokStr(b), a->length) == 0;
}

// returns -1 if local variable not found, otherwise returns slot index
// in the given compiler's locals table.
static int resolveLocal(Compiler *compiler, Token* name) {
    // Look it up in the local scopes. Look in reverse order so that the most
    // nested variable is found first and shadows outer ones.
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            return i;
        }
    }

    return -1;
}

// Attempts to look up [name] in the functions enclosing the one being compiled
// by [compiler]. If found, it adds an upvalue for it to this compiler's list
// of upvalues (unless it's already in there) and returns its index. If not
// found, returns -1.
//
// If the name is found outside of the immediately enclosing function, this
// will flatten the closure and add upvalues to all of the intermediate
// functions so that it gets walked down to this one.
static int resolveUpvalue(Compiler *compiler, Token *name) {
    COMP_TRACE("Resolving upvalue for variable '%s'", tokStr(name));
    // If we are at the top level, we didn't find it.
    if (compiler->enclosing == NULL) return -1;

    // See if it's a local variable in the immediately enclosing function.
    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        COMP_TRACE("Upvalue variable '%s' found as local", tokStr(name));
        // Mark the local as an upvalue so we know to close it when it goes out of
        // scope.
        compiler->enclosing->locals[local].isUpvalue = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }

    // See if it's an upvalue in the immediately enclosing function. In other
    // words, if it's a local variable in a non-immediately enclosing function.
    // This "flattens" closures automatically: it adds upvalues to all of the
    // intermediate functions to get from the function where a local is declared
    // all the way into the possibly deeply nested function that is closing over
    // it.
    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        COMP_TRACE("Upvalue variable '%s' found as non-local", tokStr(name));
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    // If we got here, we walked all the way up the parent chain and couldn't
    // find it.
    COMP_TRACE("Upvalue variable '%s' not found", tokStr(name));
    return -1;
}

static bool isBinOp(Insn *in) {
    bytecode_t code = in->code;
    switch (code) {
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
            return true;
        default:
            return false;
    }
}

static bool isNumConstOp(Insn *in) {
    return in->code == OP_CONSTANT && ((in->flags & INSN_FL_NUMBER) != 0);
}

static Value iseqGetConstant(Iseq *seq, bytecode_t idx) {
    return seq->constants->values[idx];
}

static Value foldConstant(Iseq *seq, Insn *cur, Insn *bin, Insn *ain) {
    Value b = iseqGetConstant(seq, bin->operands[0]);
    Value a = iseqGetConstant(seq, ain->operands[0]);
    double aNum = AS_NUMBER(a);
    double bNum = AS_NUMBER(b);
    switch (cur->code) {
        case OP_ADD:
            return NUMBER_VAL(aNum + bNum);
        case OP_SUBTRACT:
            return NUMBER_VAL(aNum - bNum);
        case OP_MULTIPLY:
            return NUMBER_VAL(aNum * bNum);
        case OP_DIVIDE:
            if (bNum == 0.00) {
                fprintf(stderr, "[Warning]: Divide by 0 found on line %d during constant folding\n", bin->lineno);
                return UNDEF_VAL;
            }
            return NUMBER_VAL(aNum / bNum);
        case OP_MODULO:
            if (bNum == 0.00) {
                fprintf(stderr, "[Warning]: Divide by 0 found on line %d during constant folding\n", bin->lineno);
                return UNDEF_VAL;
            }
            return NUMBER_VAL((int)aNum % (int)bNum);
        case OP_BITOR:
            return NUMBER_VAL((double)((int)aNum | (int)bNum));
        case OP_BITAND:
            return NUMBER_VAL((double)((int)aNum & (int)bNum));
        case OP_BITXOR:
            return NUMBER_VAL((double)((int)aNum ^ (int)bNum));
        case OP_SHOVEL_L: {
            // TODO: detect overflow for doubles
            double num = (double)((int)aNum << (int)bNum);
            return NUMBER_VAL(num);
        }
        case OP_SHOVEL_R:
            return NUMBER_VAL((double)((int)aNum >> (int)bNum));
        default:
            UNREACHABLE("bug");
    }
}

static void changeConstant(Iseq *seq, bytecode_t constIdx, Value newVal) {
    ASSERT((int)constIdx < seq->constants->count);
    seq->constants->values[constIdx] = newVal;
}

static bool isJump(Insn *in) {
    switch (in->code) {
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_TRUE:
        case OP_JUMP_IF_TRUE_PEEK:
        case OP_JUMP_IF_FALSE_PEEK:
            return true;
        default:
            return false;
    }
}

static bool isLoop(Insn *in) {
    return in->code == OP_LOOP;
}

static inline bool isJumpNextInsn(Insn *in) {
    return in->operands[0] == 0;
}

static void patchJumpInsnWithOffset(Insn *jump, int offset) {
    if (jump->operands[0]+offset > BYTECODE_MAX) {
        ASSERT(0); // TODO: error out
    }
    if (jump->operands[0]+offset <= 0) {
        ASSERT(0); // TODO: error out
    }
    jump->operands[0] += offset;
}

static void rmInsnAndPatchJumps(Iseq *seq, Insn *insn) {
    int insnIdx = iseqInsnIndex(seq, insn);
    ASSERT(insnIdx >= 0);
    OPT_DEBUG(2, "rmInsnAndPatchLabels insnIdx: %d\n", insnIdx);
    if (insnIdx == 0) {
        OPT_DEBUG(2, "insnIdx == 0\n");
        iseqRmInsn(seq, insn);
        return;
    }
    Insn *in = seq->insns;
    int idx = 0;
    while (in) {
        if (in == insn) {
            in = in->next;
            idx++;
            continue;
        }
        // jump instruction found before removed instruction, if it jumps
        // to the removed instruction or after it, then patch the jump
        if (idx < insnIdx && isJump(in)) {
            OPT_DEBUG(2, "Found jump at %d", idx);
            ASSERT(in->jumpTo);
            int jumpToIndex = iseqInsnIndex(seq, in->jumpTo);
            ASSERT(jumpToIndex >= 0);
            OPT_DEBUG(2, "Jump to index: %d", jumpToIndex);
            if (jumpToIndex >= insnIdx) {
                if (jumpToIndex == insnIdx) {
                    in->jumpTo = insn->next; // TODO: might be last instruction, in which case the jump should be removed
                }
                OPT_DEBUG(2, "Patching jump, offset before: %d", in->operands[0]);
                patchJumpInsnWithOffset(in, -(insn->numOperands+1));
                OPT_DEBUG(2, "Patched jump, offset after: %d", in->operands[0]);
            }
        // loop instruction found after removed instruction, if it jumps
        // to the removed instruction or before it, then patch the loop
        } else if (idx > insnIdx && isLoop(in)) {
            OPT_DEBUG(2, "Found loop at %d", idx);
            ASSERT(in->jumpTo);
            int jumpToIndex = iseqInsnIndex(seq, in->jumpTo);
            ASSERT(jumpToIndex >= 0);
            OPT_DEBUG(2, "Jump to index: %d", jumpToIndex);
            if (jumpToIndex <= insnIdx) {
                if (jumpToIndex == insnIdx) {
                    in->jumpTo = insn->next;
                }
                OPT_DEBUG(2, "Patching jump, offset before: %d", in->operands[0]);
                patchJumpInsnWithOffset(in, -(insn->numOperands+1));
                OPT_DEBUG(2, "Patched jump, offset after: %d", in->operands[0]);
            }
        }
        in = in->next;
        idx++;
    }
    iseqRmInsn(seq, insn);
}

static int replaceJumpInsn(Iseq *seq, Insn *jumpInsn) {
    switch (jumpInsn->code) {
        case OP_JUMP:
        case OP_JUMP_IF_FALSE_PEEK:
        case OP_JUMP_IF_TRUE_PEEK:
            if (!jumpInsn->isLabel) {
                rmInsnAndPatchJumps(seq, jumpInsn);
                return -1;
            } else {
                return 0;
            }
        case OP_JUMP_IF_FALSE:
            if (!jumpInsn->isLabel) {
                jumpInsn->code = OP_POP;
                jumpInsn->numOperands = 0;
            }
            return 0;
        default:
            UNREACHABLE("bug");
    }
}

static bool isConst(Insn *insn) {
    switch (insn->code) {
    case OP_CONSTANT:
    case OP_FALSE:
    case OP_TRUE:
    case OP_NIL:
        return true;
    }
    return false;
}

static bool constBool(Insn *insn) {
    switch (insn->code) {
    case OP_CONSTANT:
    case OP_TRUE:
        return true;
    case OP_FALSE:
    case OP_NIL:
        return false;
    default:
        UNREACHABLE("bug");
    }
}

// Array literals can have side effects, ex;
static bool noSideEffectsConst(Insn *insn) {
    return isConst(insn);
}

static inline bool isJumpIfFalse(Insn *insn) {
    return insn->code == OP_JUMP_IF_FALSE;
}

static inline bool isJumpIfTrue(Insn *insn) {
    return insn->code == OP_JUMP_IF_TRUE_PEEK;
}

static inline bool isPop(Insn *insn) {
    return insn->code == OP_POP;
}

static inline bool isPopType(Insn *insn) {
    return insn->code == OP_POP || insn->code == OP_POP_N;
}

static inline bool isJumpTarget(Insn *insn) {
    return insn->isLabel;
}

static void addInsnOperand(Iseq *seq, Insn *insn, bytecode_t operand) {
    ASSERT(insn->numOperands >= 0);
    if (insn->numOperands == MAX_INSN_OPERANDS) {
        ASSERT(0); // TODO: error out
    }
    insn->operands[insn->numOperands] = operand;
    insn->numOperands+=1;
    seq->wordCount += 1;
    int insnIdx = iseqInsnIndex(seq, insn);
    ASSERT(insnIdx >= 0);
    Insn *in = seq->insns;
    int idx = 0;
    while (in) {
        if (in == insn) {
            in = in->next;
            idx++;
            continue;
        }
        // jump instruction found before removed instruction, if it jumps
        // to the removed instruction or after it, then patch the jump
        if (idx < insnIdx && isJump(in)) {
            OPT_DEBUG(2, "Found jump at %d", idx);
            ASSERT(in->jumpTo);
            int jumpToIndex = iseqInsnIndex(seq, in->jumpTo);
            ASSERT(jumpToIndex >= 0);
            OPT_DEBUG(2, "Jump to index: %d", jumpToIndex);
            if (jumpToIndex >= insnIdx) {
                if (jumpToIndex == insnIdx) {
                    in->jumpTo = insn->next; // TODO: might be last instruction, in which case the jump should be removed
                }
                OPT_DEBUG(2, "Patching jump, offset before: %d", in->operands[0]);
                patchJumpInsnWithOffset(in, 1);
                OPT_DEBUG(2, "Patched jump, offset after: %d", in->operands[0]);
            }
        } else if (idx > insnIdx && isLoop(in)) {
            OPT_DEBUG(2, "Found loop at %d", idx);
            ASSERT(in->jumpTo);
            int jumpToIndex = iseqInsnIndex(seq, in->jumpTo);
            ASSERT(jumpToIndex >= 0);
            OPT_DEBUG(2, "Jump to index: %d", jumpToIndex);
            if (jumpToIndex <= insnIdx) {
                if (jumpToIndex == insnIdx) {
                    in->jumpTo = insn->next;
                }
                OPT_DEBUG(2, "Patching jump, offset before: %d\n", in->operands[0]);
                patchJumpInsnWithOffset(in, 1);
                OPT_DEBUG(2, "Patched jump, offset after: %d\n", in->operands[0]);
            }
        }
        in = in->next;
        idx++;
    }
}

static void optimizeIseq(Iseq *iseq) {
    COMP_TRACE("OptimizeIseq");
    Insn *cur = iseq->insns; // first insn
    Insn *prev = NULL;
    Insn *prevp = NULL;
    int idx = 0;
    while (cur) {
        COMP_TRACE("optimize idx %d", idx);
        prev = cur->prev; // can be NULL
        prevp = NULL;
        // constant folding, ex: turn 2+2 into 4
        if (isBinOp(cur)) {
            if (prev && ((prevp = prev->prev) != NULL)) {
                if (isNumConstOp(prev) && isNumConstOp(prevp)) {
                    COMP_TRACE("constant folding candidate found");
                    Value newVal = foldConstant(iseq, cur, prev, prevp);
                    if (IS_UNDEF(newVal)) { // such as divide by 0
                        cur = cur->next;
                        idx++;
                        continue;
                    }
                    changeConstant(iseq, prevp->operands[0], newVal);
                    rmInsnAndPatchJumps(iseq, cur);
                    rmInsnAndPatchJumps(iseq, prev);
                    cur = prevp;
                    idx -= 2;
                    continue;
                }
            }
        }

        // consolidate OP_POPs
        if (prev && isPopType(cur) && isPopType(prev) && !isJumpTarget(prev) && !isJumpTarget(cur)) {
            int n = cur->code == OP_POP ? 1 : cur->operands[0];
            rmInsnAndPatchJumps(iseq, cur);
            if (prev->code == OP_POP) {
                addInsnOperand(iseq, prev, n+1);
                prev->code = OP_POP_N;
            } else {
                ASSERT(prev->code == OP_POP_N);
                prev->operands[0] += n;
            }
            cur = prev;
        }

         // jump to next insn replacement/deletion
        if (isJump(cur) && isJumpNextInsn(cur)) {
            COMP_TRACE("Turning jump to next insn into POP/deletion");
            Insn *next = cur->next;
            replaceJumpInsn(iseq, cur);
            COMP_TRACE("replacement done");
            idx = 0;
            cur = next;
            continue;
        }

        // replace/remove jump instruction if test is a constant (ex: `if (true)`) =>
        // OP_TRUE, OP_POP
        if (isJump(cur) && (prev && isConst(prev))) {
            COMP_TRACE("Found constant conditional, removing/replacing JUMP");
            bool deleted = false;
            if (isJumpIfFalse(cur) && constBool(prev)) {
                deleted = replaceJumpInsn(iseq, cur) < 0;
            } else if (isJumpIfTrue(cur) && !constBool(prev)) {
                deleted = replaceJumpInsn(iseq, cur) < 0;
            }
            COMP_TRACE("/removed/replaced JUMP? %s", deleted ? "removed" : "replaced");
            if (deleted) {
                cur = iseq->insns;
                idx = 0;
                continue;
            } else {
                cur = cur->next;
                idx++;
                continue;
            }
        }

        // 1+1; OP_CONSTANT '2', OP_POP => nothing (unused constant expression)
        if (!compilerOpts.noRemoveUnusedExpressions) {
            if (isPop(cur) && (prev && noSideEffectsConst(prev))) {
                Insn *next = cur->next;
                // OP_BLOCK_CONTINUE takes the popped value from previous pop, so needs it
                if (!next || next->code != OP_BLOCK_CONTINUE) {
                    COMP_TRACE("removing side effect expr 1");
                    rmInsnAndPatchJumps(iseq, prev);
                    COMP_TRACE("removing side effect expr 2");
                    rmInsnAndPatchJumps(iseq, cur);
                    cur = iseq->insns;
                    idx = 0;
                    continue;
                }
            }
        }
        idx++;
        cur = cur->next;
    }
    COMP_TRACE("/OptimizeIseq");
}

static void copyIseqToChunk(Iseq *iseq, Chunk *chunk) {
    ASSERT(iseq);
    ASSERT(chunk);
    if (!compilerOpts.noOptimize) {
        optimizeIseq(iseq);
    }
    COMP_TRACE("copyIseqToChunk (%d insns, wordcount: %d)", iseq->count, iseq->wordCount);
    chunk->catchTbl = iseq->catchTbl;
    chunk->constants = iseq->constants;
    ASSERT(chunk->constants);
    Insn *in = iseq->insns;
    int idx = 0;
    while (in) {
        idx++;
        writeChunkWord(chunk, in->code, in->lineno, in->nlvl.depth, in->nlvl.width);
        for (int i = 0; i < in->numOperands; i++) {
            writeChunkWord(chunk, in->operands[i], in->lineno, in->nlvl.depth, in->nlvl.width);
        }
        in = in->next;
    }
    ASSERT(idx == iseq->count);
    COMP_TRACE("/copyIseqToChunk");
}

static ObjFunction *endCompiler() {
    COMP_TRACE("endCompiler");
    ASSERT(current);
    if (current->type == FUN_TYPE_TOP_LEVEL || current->type == FUN_TYPE_EVAL) {
        emitLeave();
    }
    ObjFunction *func = current->function;
    copyIseqToChunk(currentIseq(), currentChunk());
    freeTable(&current->constTbl);
    freeIseq(&current->iseq);
    func->localCount = current->localCountMax;

    current = current->enclosing;
    COMP_TRACE("/endCompiler");
    return func;
}

// Adds a constant to the current instruction sequence's constant pool
// and returns an index to it.
static bytecode_t makeConstant(Value value, ConstType ctype) {
    Value existingIdx;
    bool canMemoize = ctype == CONST_T_STRLIT;
    if (canMemoize) {
        if (tableGet(&current->constTbl, value, &existingIdx)) {
            return (bytecode_t)AS_NUMBER(existingIdx);
        }
    }
    int constant = iseqAddConstant(currentIseq(), value);
    if ((bytecode_t)constant > BYTECODE_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }
    if (canMemoize) {
        ASSERT(
            tableSet(&current->constTbl, value, NUMBER_VAL(constant))
        );
    }
    return (bytecode_t)constant;
}

// Add constant to constant pool from the token's lexeme, return index to it
static bytecode_t identifierConstant(Token *name) {
    DBG_ASSERT(vm.inited);
    ObjString *ident = INTERNED(tokStr(name), name->length);
    STRING_SET_STATIC(ident);
    return makeConstant(OBJ_VAL(ident), CONST_T_STRLIT);
}

// emits a constant instruction with the given operand
static Insn *emitConstant(Value constant, ConstType ctype) {
    Insn *ret = emitOp1(OP_CONSTANT, makeConstant(constant, ctype));
    if (ctype == CONST_T_NUMLIT) ret->flags |= INSN_FL_NUMBER;
    return ret;
}

static void emitNode(Node *n);
static void emitChildren(Node *n) {
    Node *stmt = NULL;
    int i = 0;
    nodeDepth++;
    int lastWidth = nodeWidth;
    nodeWidth = 0;
    ASSERT(n->children);
    vec_foreach(n->children, stmt, i) {
        ASSERT(stmt);
        emitNode(stmt);
    }
    nodeDepth--;
    nodeWidth = lastWidth;
}

// emit a jump (forwards) instruction, returns a pointer to the byte that needs patching
static inline Insn *emitJump(OpCode jumpOp) {
    return emitOp1(jumpOp, 0); // patched later
}

static inline Insn *emitBreak(void) {
    Insn *br = emitOp2(OP_BREAK, 0, current->localCount - loopLocalCount); // patched later
    return br;
}

static int insnOffset(Insn *start, Insn *end) {
    ASSERT(start);
    ASSERT(end);
    int offset = 0;
    Insn *cur = start;
    while (cur && cur != end) {
        offset += (cur->numOperands)+1;
        cur = cur->next;
    }
    if (cur != end) {
        ASSERT(0);
        return -1;
    }
    return offset;
}

// patch jump forwards instruction by given offset
// TODO: make the offset bigger than 1 byte!
static void patchJump(Insn *toPatch, int jumpoffset, Insn *jumpTo) {
    ASSERT(toPatch->operands[0] == 0);
    ASSERT(toPatch->jumpTo == NULL);
    if (jumpoffset == -1) {
        if (!jumpTo) {
            jumpTo = currentIseq()->tail;
        }
        jumpoffset = insnOffset(toPatch, jumpTo)+jumpTo->numOperands;
    }
    toPatch->operands[0] = jumpoffset;
    toPatch->jumpTo = jumpTo; // FIXME: should be jumpToPrev
    jumpTo->isLabel = true;
}

static inline bool isContinue(Insn *in) {
    return (in->code == OP_JUMP &&
            (in->flags & INSN_FL_CONTINUE) != 0);
}

static Insn *getInsnTail(int tail_index) {
    Insn *cur = currentIseq()->tail;
    int idx = tail_index;
    while (cur && idx > 0) {
        cur = cur->prev;
        idx--;
    }
    return cur;
}

static void patchContinuesBetween(Insn *a, Insn *b) {
  Insn *cur = a;
  while (cur != b) {
    if (isContinue(cur)) {
      int insn_tail_index = cur->extra; // number of pops to keep
      Insn *jumpTo = getInsnTail(insn_tail_index);
      patchJump(cur, -1, jumpTo);
      COMP_TRACE("jump offset found, patching continue (pops=%d)", insn_tail_index);
    }
    cur = cur->next;
  }
}

// Emit a jump backwards (loop) instruction from the current code count to offset `loopStart`
// TODO: make the offset bigger than 1 byte!
static void emitLoop(int loopStart) {

  int offset = (currentIseq()->wordCount - loopStart)+2;
  if ((bytecode_t)offset > BYTECODE_MAX) error("Loop body too large.");
  ASSERT(offset >= 0);

  Insn *loopInsn = emitOp1(OP_LOOP, offset);
  loopInsn->jumpTo = insnAtOffset(currentIseq(), loopStart-2);
  ASSERT(loopInsn->jumpTo);
  loopInsn->jumpTo->isLabel = true;
}

static inline bool isBreak(Insn *in) {
    return (in->code == OP_BREAK);
}

static void patchBreak(Insn *br, int offset, Insn *label) {
    br->operands[0] = offset;
    if (label) {
        label->isLabel = true;
    }
}

static void patchBreaks(Insn *start, Insn *end, int offset) {
    Insn *cur = start;
    int numFound = 0;
    while (cur != end) {
        if (isBreak(cur) && cur->operands[0] == 0) {
            if (offset > 0) {
                nextInsnIsLabel = true;
            }
            int off = insnOffset(cur, end)+offset;
            COMP_TRACE("break found, patching break: %d", off);
            patchBreak(cur, off, offset == 0 ? end : NULL);
            numFound++;
        }
        cur = cur->next;
    }
    COMP_TRACE("Patched %d breaks", numFound);
}

static int incrLocalCount(Compiler *compiler) {
    int oldCount = compiler->localCount;
    compiler->localCount++;
    if (compiler->localCount > compiler->localCountMax) {
        compiler->localCountMax = compiler->localCount;
    }
    return oldCount;
}

// adds local variable to current compiler's table, returns var slot
static int addLocal(Token name) {
    if (current->localCount >= UINT8_MAX) {
        error("Too many local variables");
        return -1;
    }
    Local local = {
        .name = name,
        .depth = current->scopeDepth,
        .popOnScopeEnd = true
    };
    int slot = current->localCount;
    current->locals[slot] = local;
    incrLocalCount(current);
    LocalVariable *var = ALLOCATE(LocalVariable, 1);
    var->name = copyString(tokStr(&name), strlen(tokStr(&name)), NEWOBJ_FLAG_OLD);
    var->scope = curScope;
    var->slot = slot;
    var->bytecode_declare_start = currentIseq()->wordCount;
    ObjFunction *func = currentFunction();
    vec_push(&func->variables, var);
    return slot;
}

static int addFakeLocal(bool popOnScopeEnd) {
    ASSERT(current->scopeDepth > 0);
    if (current->localCount >= UINT8_MAX) {
        error("Too many local variables");
        return -1;
    }
    Token name = syntheticToken("_");
    Local local = {
        .name = name,
        .depth = current->scopeDepth,
        .popOnScopeEnd = popOnScopeEnd
    };
    current->locals[current->localCount] = local;
    incrLocalCount(current);
    return current->localCount-1;
}

// Returns argument to give to SET_LOCAL/SET_GLOBAL, an identifier index or
// a local slot index.
static int declareVariable(Token *name) {
    if (current->scopeDepth == 0) {
        return identifierConstant(name); // global variables are implicity declared
    } else {
        // See if a local variable with this name is already declared in this scope.
        for (int i = current->localCount - 1; i >= 0; i--) {
            Local *local = &current->locals[i];
            if (local->depth != -1 && local->depth < current->scopeDepth) break;
            if (identifiersEqual(name, &local->name)) {
                error("Variable with name '%s' already defined in this scope.", tokStr(name));
                return -1;
            }
        }
        int index = addLocal(*name);
        if (index >= 0) {
          tableSet(&currentFunction()->localsTable,
              OBJ_VAL(INTERN(tokStr(name))),
              NUMBER_VAL(index));
        }
        return index;
    }
}


// emit GET/SET global or local for named variable
static void namedVariable(Token name, VarOp getSet) {
    bytecode_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    bool varNameUsed = false;
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        varNameUsed = true;
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }
    bytecode_t op = getOp;
    if (getSet == VAR_SET) { op = setOp; }
    if (varNameUsed) {
        emitOp1(op, (bytecode_t)arg);
    } else {
        bytecode_t varNameSlot = identifierConstant(&name);
        emitOp2(op, (bytecode_t)arg, varNameSlot);
        // TODO: get upvalues working
        if (op == OP_SET_LOCAL) {
            char *varName = tokStr(&name);
            addVarInfo(currentChunk(), INTERN(varName), arg);
        }
    }
}

static char *fullClassNameFromCompiler(ClassCompiler *compiler) {
    ASSERT(compiler);
    ClassCompiler *cur = compiler;
    vec_void_t v_compilers;
    vec_init(&v_compilers);
    while (cur) {
        vec_push(&v_compilers, cur);
        cur = cur->enclosing;
    }
    char *ret = NULL;
    int idx = 0;
    ClassCompiler *comp;
    vec_foreach_rev(&v_compilers, comp, idx) {
        char *buf = tokStr(&comp->name);
        size_t old_ret_size = 0;
        if (ret) {
            old_ret_size = strlen(ret)+1;
        }
        size_t new_size = old_ret_size + strlen(buf)+1;
        if (idx != 0) {
            new_size += 2; // '::'
        }
        ASSERT(new_size > 0);
        if (ret) {
            ret = realloc(ret, new_size);
        } else {
            ret = malloc(new_size);
        }
        ASSERT_MEM(ret);
        if (old_ret_size > 0) {
            memcpy(ret+old_ret_size-1, buf, strlen(buf));
        } else {
            memcpy(ret, buf, strlen(buf));
        }
        if (idx != 0) {
            ret[new_size-2] = ':';
            ret[new_size-3] = ':';
        }
        ret[new_size-1] = '\0';
        /*fprintf(stderr, "buf: '%s', old_ret_size: %d, idx: %d, v_compilers.length %d\n", buf, old_ret_size,*/
                /*idx, v_compilers.length);*/
        /*fprintf(stderr, "ret: '%s'\n", ret);*/
    }
    vec_deinit(&v_compilers);
    return ret;
}

// Initializes a new compiler for a function, and sets it as the `current`
// function compiler.
static void initCompiler(
    Compiler *compiler, // new compiler
    int scopeDepth,
    FunctionType ftype,
    Token *fTok, /* if NULL, ftype must be FUN_TYPE_TOP_LEVEL */
    Chunk *chunk /* if NULL, creates new chunk */
) {
    COMP_TRACE("initCompiler");
    memset(compiler, 0, sizeof(*compiler));
    if (ftype == FUN_TYPE_TOP_LEVEL) {
        vec_init(&compiler->v_errMessages);
    }
    compiler->enclosing = current;
    compiler->localCount = 0; // NOTE: below, this is increased to 1
    compiler->localCountMax = 0;
    compiler->scopeDepth = scopeDepth;
    compiler->function = newFunction(chunk, NULL, ftype, NEWOBJ_FLAG_OLD);
    hideFromGC(TO_OBJ(compiler->function));
    initIseq(&compiler->iseq);
    compiler->iseq.constants = compiler->function->chunk->constants;
    compiler->type = ftype;
    compiler->hadError = false;
    initTable(&compiler->constTbl);

    current = compiler;

    switch (ftype) {
    case FUN_TYPE_NAMED:
        current->function->name = copyString(
            tokStr(fTok), strlen(tokStr(fTok)),
            NEWOBJ_FLAG_OLD
        );
        OBJ_WRITE(OBJ_VAL(current->function), OBJ_VAL(current->function->name));
        break;
    case FUN_TYPE_INIT:
    case FUN_TYPE_GETTER:
    case FUN_TYPE_SETTER:
    case FUN_TYPE_METHOD:
    case FUN_TYPE_CLASS_METHOD: {
        ASSERT(currentClassOrModule || inINBlock);
        char *className = "";
        if (currentClassOrModule) {
            className = fullClassNameFromCompiler(currentClassOrModule);
        }
        char *funcName = tokStr(fTok);
        size_t methodNameBuflen = strlen(className)+1+strlen(funcName)+1; // +1 for '.' in between
        char *methodNameBuf = xcalloc(1, methodNameBuflen);
        ASSERT_MEM(methodNameBuf);
        strcpy(methodNameBuf, className);
        char *sep = "#";
        if (ftype == FUN_TYPE_CLASS_METHOD) {
            sep = ".";
        }
        strncat(methodNameBuf, sep, 1);
        strcat(methodNameBuf, funcName);
        ObjString *methodName = copyString(methodNameBuf, strlen(methodNameBuf), NEWOBJ_FLAG_OLD);
        current->function->name = methodName;
        OBJ_WRITE(OBJ_VAL(current->function), OBJ_VAL(methodName));
        xfree(methodNameBuf);
        if (strlen(className) > 0) {
            xfree(className);
        }
        break;
    }
    case FUN_TYPE_ANON:
    case FUN_TYPE_BLOCK:
    case FUN_TYPE_TOP_LEVEL:
        current->function->name = NULL;
        if (ftype == FUN_TYPE_BLOCK) {
            current->function->isBlock = true;
        }
        break;
    default:
        UNREACHABLE("invalid function type %d", ftype);
    }

    // The first local variable slot is always implicitly declared, unless
    // we're in the function representing the top-level (main).
    if (ftype != FUN_TYPE_TOP_LEVEL) {
        Local *local = &current->locals[incrLocalCount(current)];
        local->depth = current->scopeDepth;
        local->isUpvalue = false;
        local->popOnScopeEnd = true;
        if (ftype == FUN_TYPE_METHOD || ftype == FUN_TYPE_INIT ||
                ftype == FUN_TYPE_GETTER || ftype == FUN_TYPE_SETTER ||
                ftype == FUN_TYPE_CLASS_METHOD) {
            current->function->hasReceiver = true;
            local->name.lexeme = ""; // represents `this`
            local->name.length = 0;
        } else {
            // In a function, it holds the function object, but cannot be referenced, so has
            // no name.
            local->name.lexeme = "";
            local->name.length = 0;
        }
    }
    COMP_TRACE("/initCompiler");
}

// Initializes a new compiler for a function, and sets it as the `current`
// function compiler.
static void initEvalCompiler(
    Compiler *compiler, // new compiler
    ObjFunction *in_func,
    bytecode_t *ip_at
) {
    COMP_TRACE("initCompiler");
    memset(compiler, 0, sizeof(*compiler));
    vec_init(&compiler->v_errMessages);
    compiler->enclosing = NULL;
    compiler->localCount = 0; // NOTE: below, this is increased to whatever it needs to be
    compiler->localCountMax = 0;
    if (in_func->ftype == FUN_TYPE_TOP_LEVEL) {
        compiler->scopeDepth = 0;
    } else {
        compiler->scopeDepth = 1;
    }
    compiler->function = newFunction(NULL, NULL, FUN_TYPE_EVAL, NEWOBJ_FLAG_OLD);
    hideFromGC(TO_OBJ(compiler->function));
    initIseq(&compiler->iseq);
    compiler->iseq.constants = compiler->function->chunk->constants;
    compiler->type = FUN_TYPE_EVAL;
    compiler->hadError = false;
    initTable(&compiler->constTbl);

    current = compiler;
    current->function->name = NULL;

    // The first local variable slot is always implicitly declared, unless
    // we're in the function representing the top-level (main).
    if (in_func->ftype != FUN_TYPE_TOP_LEVEL) {
        Local *local = &current->locals[incrLocalCount(current)];
        local->depth = current->scopeDepth;
        local->isUpvalue = false;
        local->popOnScopeEnd = true;
        if (in_func->ftype == FUN_TYPE_METHOD || in_func->ftype == FUN_TYPE_INIT ||
                in_func->ftype == FUN_TYPE_GETTER || in_func->ftype == FUN_TYPE_SETTER ||
                in_func->ftype == FUN_TYPE_CLASS_METHOD) {
            in_func->hasReceiver = true;
            local->name.lexeme = ""; // represents `this`
            local->name.length = 0;
        } else {
            // In a function, it holds the function object, but cannot be referenced, so has
            // no name.
            local->name.lexeme = "";
            local->name.length = 0;
        }
    }

    int bytecode_at = ip_at - in_func->chunk->code;
    LocalVariable *var; int varidx = 0;
    vec_foreach(&in_func->variables, var, varidx) {
        VM_DEBUG(2, "Maybe adding local %s to eval callframe", var->name->chars);
        if (var->bytecode_declare_start < bytecode_at) {
            VM_DEBUG(2, "adding local %s to eval callframe", var->name->chars);
            Local *local = &current->locals[incrLocalCount(current)];
            local->name.lexeme = var->name->chars;
            local->name.length = strlen(var->name->chars);
            local->depth = current->scopeDepth;
            local->popOnScopeEnd = true;
            local->isUpvalue = false;
        } else {
        }
    }

    COMP_TRACE("/initCompiler");
}

static void initBindingEvalCompiler(
    Compiler *compiler, // new compiler
    ObjScope *scope
) {
    COMP_TRACE("initCompiler");
    memset(compiler, 0, sizeof(*compiler));
    vec_init(&compiler->v_errMessages);
    compiler->enclosing = NULL;
    compiler->localCount = 0; // NOTE: below, this is increased to whatever it needs to be
    compiler->localCountMax = 0;
    if (scope->function->ftype == FUN_TYPE_TOP_LEVEL) {
        compiler->scopeDepth = 0;
    } else {
        compiler->scopeDepth = 1;
    }
    compiler->function = newFunction(NULL, NULL, FUN_TYPE_EVAL, NEWOBJ_FLAG_OLD);
    hideFromGC(TO_OBJ(compiler->function));
    initIseq(&compiler->iseq);
    compiler->iseq.constants = compiler->function->chunk->constants;
    compiler->type = FUN_TYPE_EVAL;
    compiler->hadError = false;
    initTable(&compiler->constTbl);

    current = compiler;
    current->function->name = NULL;

    // The first local variable slot is always implicitly declared, unless
    // we're in the function representing the top-level (main).
    if (scope->function->ftype != FUN_TYPE_TOP_LEVEL) {
        Local *local = &current->locals[incrLocalCount(current)];
        local->depth = current->scopeDepth;
        local->isUpvalue = false;
        local->popOnScopeEnd = true;
        int ftype = scope->function->ftype;
        if (ftype == FUN_TYPE_METHOD || ftype == FUN_TYPE_INIT ||
                ftype == FUN_TYPE_GETTER || ftype == FUN_TYPE_SETTER ||
                ftype == FUN_TYPE_CLASS_METHOD) {
            compiler->function->hasReceiver = true;
            local->name.lexeme = ""; // represents `this`
            local->name.length = 0;
        } else {
            // In a function, it holds the function object, but cannot be referenced, so has
            // no name.
            local->name.lexeme = "";
            local->name.length = 0;
        }
    }

    Entry e; int varidx = 0;
    TABLE_FOREACH(&scope->function->localsTable, e, varidx, {
        Local *local = &current->locals[incrLocalCount(current)];
        local->name.lexeme = AS_CSTRING(e.key);
        local->name.length = strlen(local->name.lexeme);
        local->depth = current->scopeDepth;
        local->isUpvalue = false;
        local->popOnScopeEnd = true;
    });

    COMP_TRACE("/initCompiler");
}

// Define a declared variable in local or global scope (locals MUST be
// declared before being defined)
static void defineVariable(Token *name, bytecode_t arg, bool checkDecl) {
  (void)name;
  if (current->scopeDepth == 0) {
    emitOp1(OP_DEFINE_GLOBAL, arg);
  } else {
    // Mark the given local as defined now (-1 is undefined, but declared)
    /*if (current->locals[arg].depth != -1 && checkDecl) {*/
        /*error("undeclared local variable [slot %d], scope depth: %d", arg, current->scopeDepth);*/
    /*}*/
    current->locals[arg].depth = current->scopeDepth;
  }
}

static void emitClass(Node *n) {
    bytecode_t nameConstant = identifierConstant(&n->tok);
    ClassCompiler cComp;
    memset(&cComp, 0, sizeof(cComp));
    cComp.name = n->tok;
    Token *superClassTok = (Token*)nodeGetData(n);
    cComp.hasSuperclass = superClassTok != NULL;
    cComp.isModule = false;
    cComp.enclosing = currentClassOrModule;
    currentClassOrModule = &cComp;

    pushScope(COMPILE_SCOPE_CLASS);
    if (cComp.hasSuperclass) {
        namedVariable(*superClassTok, VAR_GET);
        emitOp1(OP_SUBCLASS, nameConstant); // VM peeks the superclass and gets the class name
    } else {
        emitOp1(OP_CLASS, nameConstant); // VM gets the class name
    }

    addFakeLocal(false); // fake local corresponds to `this` (slot 0)
    emitChildren(n); // block node with methods and other declarations

    popScope(COMPILE_SCOPE_CLASS);

    emitOp0(OP_POP_CREF);
    currentClassOrModule = cComp.enclosing;
}

static void emitModule(Node *n) {
    bytecode_t nameConstant = identifierConstant(&n->tok);

    ClassCompiler cComp;
    memset(&cComp, 0, sizeof(cComp));
    cComp.name = n->tok;
    cComp.hasSuperclass = false;
    cComp.isModule = true;
    cComp.enclosing = currentClassOrModule;
    currentClassOrModule = &cComp;

    emitOp1(OP_MODULE, nameConstant);
    pushScope(COMPILE_SCOPE_MODULE);
    emitChildren(n);
    popScope(COMPILE_SCOPE_MODULE);

    // define the local or global variable for the class itself
    /*if (current->scopeDepth == 0) {*/
        /*emitOp1(OP_SET_CONST, nameConstant);*/
    /*} else {*/
        /*uint8_t defineArg = identifierConstant(&n->tok);*/
        /*emitOp1(OP_SET_CONST, defineArg);*/
    /*}*/
    emitOp0(OP_POP_CREF);
    currentClassOrModule = cComp.enclosing;
}

static void emitIn(Node *n) {
    bool oldIn = inINBlock;
    emitNode(n->children->data[0]); // expression
    emitOp0(OP_IN); // sets vm.self properly
    inINBlock = true;
    pushScope(COMPILE_SCOPE_IN);
    emitNode(n->children->data[1]); // block
    popScope(COMPILE_SCOPE_IN);
    emitOp0(OP_POP);
    inINBlock = oldIn;
}

static CallInfo *emitCall(Node *n) {
    int nArgs = n->children->length-1;
    // arbitrary, but we don't want the VM op stack to blow by pushing a whole
    // bunch of arguments
    if (nArgs > 8) {
        error("too many arguments given to function (%d), maximum 8", nArgs);
        return NULL;
    }
    Node *arg = NULL;
    int i = 0;
    Node *lhs = vec_first(n->children);
    int argc = n->children->length; // num regular arguments
    int numKwargs = 0;
    bool usesSplat = false;
    CallInfo *callInfoData;
    if (nodeKind(lhs) == PROP_ACCESS_EXPR) {
        emitChildren(lhs); // the instance
        bytecode_t methodNameArg = identifierConstant(&lhs->tok);
        vec_foreach(n->children, arg, i) {
            if (i == 0) continue;
            if (arg->type.kind == KWARG_IN_CALL_STMT) {
                argc--;
                numKwargs++;
            } else if (arg->type.kind == SPLAT_EXPR) {
                usesSplat = true;
                argc--;
            }
            emitNode(arg);
        }
        callInfoData = ALLOCATE(CallInfo, 1);
        memset(callInfoData, 0, sizeof(CallInfo));
        callInfoData->nameTok = n->tok;
        callInfoData->argc = argc;
        callInfoData->numKwargs = numKwargs;
        callInfoData->usesSplat = usesSplat;
        i = 0; int idx = 0;
        vec_foreach(n->children, arg, i) {
            if (arg->type.kind == KWARG_IN_CALL_STMT) {
                callInfoData->kwargNames[idx] = arg->tok;
                idx++;
            }
        }
        ObjInternal *callInfoObj = newInternalObject(true, callInfoData, sizeof(CallInfo), NULL, NULL, NEWOBJ_FLAG_OLD);
        hideFromGC(TO_OBJ(callInfoObj));
        bytecode_t callInfoConstSlot = makeConstant(OBJ_VAL(callInfoObj), CONST_T_CALLINFO);
        emitOp3(OP_INVOKE, methodNameArg, nArgs, callInfoConstSlot);
    } else {
        emitNode(lhs); // the function itself
        i = 0;
        vec_foreach(n->children, arg, i) {
            if (i == 0) continue;
            if (arg->type.kind == KWARG_IN_CALL_STMT) {
                argc--;
                numKwargs++;
            } else if (arg->type.kind == SPLAT_EXPR) {
                usesSplat = true;
                argc--;
            }
            emitNode(arg);
        }
        callInfoData = ALLOCATE(CallInfo, 1);
        ASSERT_MEM(callInfoData);
        memset(callInfoData, 0, sizeof(CallInfo));
        callInfoData->nameTok = n->tok;
        callInfoData->argc = argc;
        callInfoData->numKwargs = numKwargs;
        callInfoData->usesSplat = usesSplat;
        i = 0; int idx = 0;
        vec_foreach(n->children, arg, i) {
            if (arg->type.kind == KWARG_IN_CALL_STMT) {
                callInfoData->kwargNames[idx] = arg->tok;
                idx++;
            }
        }
        ObjInternal *callInfoObj = newInternalObject(true, callInfoData, sizeof(CallInfo), NULL, NULL, NEWOBJ_FLAG_OLD);
        hideFromGC(TO_OBJ(callInfoObj));
        bytecode_t callInfoConstSlot = makeConstant(OBJ_VAL(callInfoObj), CONST_T_CALLINFO);
        emitOp2(OP_CALL, (bytecode_t)nArgs, callInfoConstSlot);
    }
    return callInfoData;
}

// emit function or method
static ObjFunction *emitFunction(Node *n, FunctionType ftype) {
    Compiler fCompiler;
    int scopeDepth = current->scopeDepth;
    initCompiler(&fCompiler, scopeDepth, ftype, &n->tok, NULL);
    CompileScopeType stype = COMPILE_SCOPE_FUNCTION;

    if (ftype == FUN_TYPE_TOP_LEVEL) {
      stype = COMPILE_SCOPE_MAIN;
    }
    pushScope(stype);
    ObjFunction *func = fCompiler.function;
    ASSERT(func);
    func->funcNode = n;

    vec_nodep_t *params = (vec_nodep_t*)nodeGetData(n);
    ASSERT(params);
    Node *param = NULL; int i = 0;
    vec_foreach(params, param, i) {
        if (param->type.kind == PARAM_NODE_REGULAR) {
            bytecode_t localSlot = declareVariable(&param->tok);
            defineVariable(&param->tok, localSlot, true);
            func->arity++;
        }
    }
    // optional arguments gets pushed in order so we can skip the local var set code when necessary
    // (when the argument is given during the call).
    int numParams = params->length;
    bool hasKwarg = false;
    param = NULL; i = 0;
    vec_foreach(params, param, i) {
        if (param->type.kind == PARAM_NODE_DEFAULT_ARG) {
            uint8_t localSlot = declareVariable(&param->tok);
            defineVariable(&param->tok, localSlot, true);
            func->numDefaultArgs++;
            Insn *insnBefore = currentIseq()->tail; // NOTE: can be NULL
            emitNode(vec_first(param->children)); // default arg
            // the VM skips these instructions if the argument is supplied
            emitOp2(OP_SET_LOCAL, (bytecode_t)localSlot, identifierConstant(&param->tok));
            emitOp0(OP_POP);
            Insn *insnAfter = currentIseq()->tail;
            size_t codeDiff = iseqInsnWordDiff(insnBefore, insnAfter);
            // TODO: use ALLOCATE, and make sure to free when freeing the node
            ParamNodeInfo *paramNodeInfo = xcalloc(1, sizeof(ParamNodeInfo));
            ASSERT_MEM(paramNodeInfo);
            paramNodeInfo->defaultArgIPOffset = codeDiff;
            ASSERT(param->data == NULL);
            param->data = paramNodeInfo;
        } else if (param->type.kind == PARAM_NODE_SPLAT) {
            uint8_t localSlot = declareVariable(&param->tok);
            defineVariable(&param->tok, localSlot, true);
            func->hasRestArg = true;
        } else if (param->type.kind == PARAM_NODE_KWARG) {
            hasKwarg = true;
            uint8_t localSlot = declareVariable(&param->tok);
            defineVariable(&param->tok, localSlot, true);
            emitOp2(OP_CHECK_KEYWORD,
                (bytecode_t)localSlot /* slot of keyword argument */,
                // NOTE: if a function is called with a block argument, the VM
                // must adjust its OP_CHECK_KEYWORD stack check by 1
                numParams+1 /* slot of keyword map */
            );
            func->numKwargs++;
            Insn *ifJumpStart = emitJump(OP_JUMP_IF_TRUE);
            emitChildren(param);
            emitOp2(OP_SET_LOCAL, localSlot, identifierConstant(&param->tok));
            emitOp0(OP_POP);
            patchJump(ifJumpStart, -1, NULL);
        } else if (param->type.kind == PARAM_NODE_BLOCK) { // &arg
            uint8_t localSlot = declareVariable(&param->tok);
            defineVariable(&param->tok, localSlot, true);
            func->hasBlockArg = true;
        } else if (param->type.kind == PARAM_NODE_REGULAR) {
            // handled above in first loop
        } else {
            UNREACHABLE("Unknown parameter type (kind): %d", param->type.kind);
        }
    }

    if (hasKwarg) {
        addFakeLocal(true);
    }

    bool oldBreakBlock = breakBlock;
    if (ftype == FUN_TYPE_BLOCK) {
        blockDepth++;
        breakBlock = true;
    }
    eLoopType oldLoopType = curLoopType;
    curLoopType = LOOP_T_BLOCK;
    emitChildren(n);
    curLoopType = oldLoopType;
    if (ftype == FUN_TYPE_BLOCK) {
        blockDepth--;
        breakBlock = oldBreakBlock;
    }
    popScope(stype);
    func = endCompiler();
    ASSERT(func->chunk);

    // save the chunk as a constant in the parent (now current) chunk
    if (ftype != FUN_TYPE_BLOCK) {
        bytecode_t funcIdx = makeConstant(OBJ_VAL(func), CONST_T_CODE);
        emitOp1(OP_CLOSURE, funcIdx);
    }
    // Emit arguments for each upvalue to know whether to capture a local or
    // an upvalue.
    func->upvaluesInfo = ALLOCATE(Upvalue, LX_MAX_UPVALUES);
    ASSERT_MEM(func->upvaluesInfo);
    for (int i = 0; i < func->upvalueCount; i++) {
        if (ftype != FUN_TYPE_BLOCK) {
            emitOp0(fCompiler.upvalues[i].isLocal ? 1 : 0);
            emitOp0(fCompiler.upvalues[i].index);
        }
        func->upvaluesInfo[i] = fCompiler.upvalues[i]; // copy upvalue info
    }

    if (ftype == FUN_TYPE_TOP_LEVEL || ftype == FUN_TYPE_BLOCK ||
            ftype == FUN_TYPE_ANON) {
        return func;
    }

    if ((currentClassOrModule == NULL && !inINBlock) || ftype == FUN_TYPE_NAMED) { // regular function
        namedVariable(n->tok, VAR_SET);
        emitOp0(OP_POP);
    } else { // method
        switch (ftype) {
            case FUN_TYPE_METHOD:
            case FUN_TYPE_INIT:
                emitOp1(OP_METHOD, identifierConstant(&n->tok));
                break;
            case FUN_TYPE_CLASS_METHOD:
                emitOp1(OP_CLASS_METHOD, identifierConstant(&n->tok));
                break;
            case FUN_TYPE_GETTER:
                emitOp1(OP_GETTER, identifierConstant(&n->tok));
                break;
            case FUN_TYPE_SETTER:
                emitOp1(OP_SETTER, identifierConstant(&n->tok));
                break;
            default:
                UNREACHABLE("bug: invalid function type: %d\n", ftype);
        }
    }
    return func;
}

static void pushVarSlots() {
    addFakeLocal(true);
}

static void emitBinaryOp(Token tok) {
    if (tok.type == TOKEN_PLUS) {
        emitOp0(OP_ADD);
    } else if (tok.type == TOKEN_MINUS) {
        emitOp0(OP_SUBTRACT);
    } else if (tok.type == TOKEN_STAR) {
        emitOp0(OP_MULTIPLY);
    } else if (tok.type == TOKEN_SLASH) {
        emitOp0(OP_DIVIDE);
    } else if (tok.type == TOKEN_PERCENT) {
        emitOp0(OP_MODULO);
    } else if (tok.type == TOKEN_LESS) {
        emitOp0(OP_LESS);
    } else if (tok.type == TOKEN_LESS_EQUAL) {
        emitOp0(OP_LESS_EQUAL);
    } else if (tok.type == TOKEN_GREATER) {
        emitOp0(OP_GREATER);
    } else if (tok.type == TOKEN_GREATER_EQUAL) {
        emitOp0(OP_GREATER_EQUAL);
    } else if (tok.type == TOKEN_EQUAL_EQUAL) {
        emitOp0(OP_EQUAL);
    } else if (tok.type == TOKEN_BANG_EQUAL) {
        emitOp0(OP_NOT_EQUAL);
    } else if (tok.type == TOKEN_PIPE) {
        emitOp0(OP_BITOR);
    } else if (tok.type == TOKEN_AMP) {
        emitOp0(OP_BITAND);
    } else if (tok.type == TOKEN_CARET) {
        emitOp0(OP_BITXOR);
    } else if (tok.type == TOKEN_SHOVEL_L) {
        emitOp0(OP_SHOVEL_L);
    } else if (tok.type == TOKEN_SHOVEL_R) {
        emitOp0(OP_SHOVEL_R);
    } else {
        UNREACHABLE("invalid binary expr node (token: %s)", tokStr(&tok));
    }
}

static bool isConstExprNodeCanEmbed(Node *n) {
    switch (nodeKind(n)) {
        case LITERAL_EXPR: {
            switch (n->tok.type) {
                case TOKEN_NUMBER:
#ifdef NAN_TAGGING
                case TOKEN_TRUE:
                case TOKEN_FALSE:
                case TOKEN_NIL:
#endif
                    return true;
                default:
                    return false;
            }
        }
        default:
            return false;
    }
}

static Value numberLiteral(Node *n, bool emit) {
    const char *numStr = tokStr(&n->tok);
    size_t numLen = strlen(numStr);
    double d = 0.00;
    // octal number
    if (numLen >= 2 && numStr[0] == '0' && (numStr[1] == 'c' || numStr[1] == 'C')) {
        d = (double)strtol(numStr+2, NULL, 8);
        // hex number
    } else if (numLen >= 2 && numStr[0] == '0' && (numStr[1] == 'x' || numStr[1] == 'X')) {
        d = strtod(numStr, NULL);
        // binary number
    } else if (numLen >= 2 && numStr[0] == '0' && (numStr[1] == 'b' || numStr[1] == 'B')) {
        d = (double)strtol(numStr+2, NULL, 2);
    } else { // decimal number
        if (numStr[0] == '0' && numLen > 1) {
            int line = curTok ? curTok->line : 0;
            fprintf(stderr, "[Warning]: Decimal (base 10) number starting with '0' "
                    "found on line %d. If you wanted an octal number, the prefix is '0c' (ex: 0c644).\n", line);
        }
        d = strtod(numStr, NULL);
    }
    Value numVal = NUMBER_VAL(d);
    if (emit) {
        emitConstant(numVal, CONST_T_NUMLIT);
        return UNDEF_VAL;
    } else {
        return numVal;
    }
}

static Value valueFromConstNode(Node *n) {
    switch (nodeKind(n)) {
        case LITERAL_EXPR: {
            switch (n->tok.type) {
                case TOKEN_NUMBER:
                    return numberLiteral(n, false);
#ifdef NAN_TAGGING
                case TOKEN_TRUE:
                    return TRUE_VAL;
                case TOKEN_FALSE:
                    return FALSE_VAL;
                case TOKEN_NIL:
                    return NIL_VAL;
#endif
                default:
                    UNREACHABLE("bug");
            }
        }
        default:
            UNREACHABLE("bug");
    }
}


static void emitNode(Node *n) {
    if (current->hadError) return;
    curTok = &n->tok;
    nodeWidth++;
    switch (nodeKind(n)) {
    case STMTLIST_STMT:
    case GROUPING_EXPR: {
        emitChildren(n);
        break;
    }
    case EXPR_STMT: {
        emitChildren(n);
        emitOp0(OP_POP);
        break;
    }
    case BINARY_EXPR:
    case BINARY_ASSIGN_EXPR: {
        emitChildren(n);
        emitBinaryOp(n->tok);
        if (nodeKind(n) == BINARY_ASSIGN_EXPR) {
            Node *varNode = vec_first(n->children);
            namedVariable(varNode->tok, VAR_SET);
        }
        break;
    }
    case LOGICAL_EXPR: {
        if (n->tok.type == TOKEN_AND) {
            emitNode(vec_first(n->children)); // lhs
            // false and "hi"
            Insn *skipRhsJump = emitJump(OP_JUMP_IF_FALSE_PEEK);
            emitNode(vec_last(n->children)); // rhs
            emitOp0(OP_AND);
            patchJump(skipRhsJump, insnOffset(skipRhsJump, currentIseq()->tail), currentIseq()->tail);
        } else if (n->tok.type == TOKEN_OR) {
            emitNode(vec_first(n->children)); // lhs
            Insn *skipRhsJump = emitJump(OP_JUMP_IF_TRUE_PEEK);
            emitNode(vec_last(n->children)); // rhs
            emitOp0(OP_OR);
            patchJump(skipRhsJump, insnOffset(skipRhsJump, currentIseq()->tail), currentIseq()->tail);
        } else {
            UNREACHABLE("invalid logical expression node (token: %s)", tokStr(&n->tok));
        }
        break;
    }
    case UNARY_EXPR: {
        emitNode(n->children->data[0]);
        if (n->tok.type == TOKEN_MINUS) {
            emitOp0(OP_NEGATE);
        } else if (n->tok.type == TOKEN_BANG) {
            emitOp0(OP_NOT);
        } else {
            UNREACHABLE("invalid unary expr node (token: %s)", tokStr(&n->tok));
        }
        break;
    }
    case LITERAL_EXPR: {
        if (n->tok.type == TOKEN_NUMBER) {
            numberLiteral(n, true);
        } else if (n->type.litKind == REGEX_TYPE) {
            Token *regex = &n->tok;
            ObjString *reStr = INTERNED(tokStr(regex), regex->length);
            STRING_SET_STATIC(reStr);
            bytecode_t strSlot = makeConstant(OBJ_VAL(reStr), CONST_T_STRLIT);
            emitOp1(OP_REGEX, strSlot);
        // non-static string
        } else if (n->tok.type == TOKEN_STRING_SQUOTE || n->tok.type == TOKEN_STRING_DQUOTE) {
            Token *name = &n->tok;
            ObjString *str = INTERNED(tokStr(name), name->length);
            STRING_SET_STATIC(str);
            bytecode_t strSlot = makeConstant(OBJ_VAL(str), CONST_T_STRLIT);
            emitOp2(OP_STRING, strSlot, 0);
        // static string
        } else if (n->tok.type == TOKEN_STRING_STATIC) {
            Token *name = &n->tok;
            ObjString *str = INTERNED(tokStr(name), name->length);
            STRING_SET_STATIC(str);
            bytecode_t strSlot = makeConstant(OBJ_VAL(str), CONST_T_STRLIT);
            emitOp2(OP_STRING, strSlot, 1);
        } else if (n->tok.type == TOKEN_TRUE) {
            emitOp0(OP_TRUE);
        } else if (n->tok.type == TOKEN_FALSE) {
            emitOp0(OP_FALSE);
        } else if (n->tok.type == TOKEN_NIL) {
            emitOp0(OP_NIL);
        } else {
            UNREACHABLE("invalid literal expr node (token: %s)", tokStr(&n->tok));
        }
        break;
    }
    case ARRAY_EXPR: {
        if (n->children->length >= UINT8_MAX) {
            // TODO: fix
            error("Too many elements in array literal");
            return;
        }
        Node *elNode = NULL; int elIdx = 0;
        bool allConst = true;
        vec_foreach(n->children, elNode, elIdx) {
            if (!isConstExprNodeCanEmbed(elNode)) {
                allConst = false;
                break;
            }
        }
        if (!allConst) {
            vec_reverse(n->children);
            emitChildren(n);
            emitOp1(OP_ARRAY, (bytecode_t)n->children->length);
        } else {
            elNode = NULL; elIdx = 0;
            Value ary = newArrayConstant();
            // NOTE: no need for OBJ_WRITE here, elements are always non-objects
            // right now for array constants.
            hideFromGC(AS_OBJ(ary));
            vec_foreach(n->children, elNode, elIdx) {
                arrayPush(ary, valueFromConstNode(elNode));
            }
            bytecode_t arySlot = makeConstant(ary, CONST_T_ARYLIT);
            emitOp1(OP_DUPARRAY, arySlot);
        }
        break;
    }
    case MAP_EXPR: {
        ASSERT(n->children->length % 2 == 0);
        if (n->children->length >= UINT8_MAX) {
            // TODO: fix
            error("Too many key-value pairs in map literal");
            return;
        }
        Node *elNode = NULL; int elIdx = 0;
        bool allConst = true;
        vec_foreach(n->children, elNode, elIdx) {
            if (!isConstExprNodeCanEmbed(elNode)) {
                allConst = false;
                break;
            }
        }

        if (!allConst) {
            vec_reverse(n->children);
            emitChildren(n);
            emitOp1(OP_MAP, (bytecode_t)n->children->length);
        } else {
            elNode = NULL; elIdx = 0;
            Value map = newMapConstant();
            hideFromGC(AS_OBJ(map));
            Node *lastNode = NULL;
            vec_foreach(n->children, elNode, elIdx) {
                if (elIdx % 2 != 0 && elIdx > 0) {
                    mapSet(map, valueFromConstNode(lastNode), valueFromConstNode(elNode));
                }
                lastNode = elNode;
            }
            bytecode_t mapSlot = makeConstant(map, CONST_T_MAPLIT);
            emitOp1(OP_DUPMAP, mapSlot);
        }

        break;
    }
    case IF_STMT: {
        emitNode(n->children->data[0]); // condition
        Insn *ifJumpStart = emitJump(OP_JUMP_IF_FALSE);
        pushScope(COMPILE_SCOPE_IF);
        emitNode(n->children->data[1]); // then branch
        Node *elseNode = NULL;
        if (n->children->length > 2) {
            elseNode = n->children->data[2];
        }
        if (elseNode == NULL) {
            patchJump(ifJumpStart, -1, NULL);
        } else {
            Insn *elseJump = emitJump(OP_JUMP);
            patchJump(ifJumpStart, -1, NULL);
            emitNode(elseNode);
            patchJump(elseJump, -1, NULL);
        }
        popScope(COMPILE_SCOPE_IF);
        break;
    }
    case WHILE_STMT: {
        int oldLoopStart = loopStart;
        bool oldBreakBlock = breakBlock;
        breakBlock = false;
        Insn *loopLabel = currentIseq()->tail;
        loopStart = currentIseq()->wordCount+2;
        emitNode(vec_first(n->children)); // cond
        if (loopLabel) {
            loopLabel = loopLabel->next; // beginning of conditional
        } else {
            loopLabel = currentIseq()->tail;
        }
        Insn *whileJumpStart = emitJump(OP_JUMP_IF_FALSE);
        whileJumpStart->isLabel = true;
        eLoopType oldLoopType = curLoopType;
        curLoopType = LOOP_T_WHILE;
        pushScope(COMPILE_SCOPE_WHILE);
        emitNode(n->children->data[1]); // while block
        curLoopType = oldLoopType;
        loopLabel->jumpTo = whileJumpStart;
        whileJumpStart->isLabel = true;
        emitLoop(loopStart);
        patchJump(whileJumpStart, -1, NULL);
        patchBreaks(whileJumpStart, currentIseq()->tail, 1);
        loopStart = oldLoopStart;
        breakBlock = oldBreakBlock;
        popScope(COMPILE_SCOPE_WHILE);
        break;
    }
    case FOR_STMT: {
        pushScope(COMPILE_SCOPE_FOR);
        Node *init = vec_first(n->children);
        if (init) {
            emitNode(init);
        }
        int oldLoopStart = loopStart;
        bool oldBreakBlock = breakBlock;
        Node *test = n->children->data[1];
        int beforeTest = currentIseq()->wordCount+2;
        loopStart = beforeTest;
        if (test) {
            emitNode(test);
        } else {
            emitOp0(OP_TRUE);
        }
        Insn *forJump = emitJump(OP_JUMP_IF_FALSE);
        Node *forBlock = vec_last(n->children);
        eLoopType lastLoopType = curLoopType;
        curLoopType = LOOP_T_FOR;
        emitNode(forBlock);
        curLoopType = lastLoopType;
        Node *incrExpr = n->children->data[2];
        if (incrExpr) {
            emitNode(incrExpr);
        }
        emitOp0(OP_POP);
        emitLoop(beforeTest);
        patchJump(forJump, -1, NULL);
        patchBreaks(forJump, currentIseq()->tail, 2);
        loopStart = oldLoopStart;
        breakBlock = oldBreakBlock;
        popScope(COMPILE_SCOPE_FOR);
        break;
    }
    case FOREACH_STMT: {
        pushScope(COMPILE_SCOPE_FOREACH);
        vec_byte_t v_slots;
        vec_init(&v_slots);
        int numVars = n->children->length - 2;
        pushVarSlots(); // don't overwrite the iterator with the variables
        if (numVars > 1) {
            pushVarSlots(); // the iterator value
        }
        int i = 0;
        for (i = 0; i < numVars; i++) {
            Token varName = n->children->data[i]->tok;
            uint8_t varSlot = declareVariable(&varName);
            vec_push(&v_slots, varSlot);
        }
        // iterator expression
        emitNode(n->children->data[i]);
        i++;

        emitOp0(OP_ITER); // push iterator to stack
        int beforeIterNext = currentIseq()->wordCount+2;
        emitOp0(OP_ITER_NEXT);
        // TODO: op_jump_if_undef? Otherwise, nil or false marks end of iteration
        Insn *iterDone = emitJump(OP_JUMP_IF_FALSE_PEEK);
        bytecode_t slotNum = 0; int slotIdx = 0;
        int setOp = numVars > 1 ? OP_UNPACK_SET_LOCAL : OP_SET_LOCAL;
        vec_foreach(&v_slots, slotNum, slotIdx) {
            Token name = n->children->data[slotIdx]->tok;
            bytecode_t nameIdx = identifierConstant(&name);
            if (setOp == OP_SET_LOCAL) {
                emitOp2(setOp, slotNum, nameIdx);
            } else { // SET_UNPACK
                emitOp3(setOp, slotNum, (bytecode_t)slotIdx, nameIdx);
            }
        }
        Insn *beforeForeach = currentIseq()->tail;
        eLoopType lastLoopType = curLoopType;
        curLoopType = LOOP_T_FOREACH;

        int oldLoopLocalCount = loopLocalCount;
        loopLocalCount = current->localCount;
        emitNode(n->children->data[i]); // foreach block
        loopLocalCount = oldLoopLocalCount;

        curLoopType = lastLoopType;
        Insn *afterForeach = currentIseq()->tail;
        patchContinuesBetween(beforeForeach, afterForeach);
        if (numVars == 1) {
            emitOp0(OP_POP); // pop the iterator value
        }
        if (numVars > 1) {
            for (int j = 0; j < numVars; j++) {
                emitOp0(OP_POP); // pop the iterator values
            }
            emitOp0(OP_POP); // pop the iterator value array, for unpack
        }
        emitLoop(beforeIterNext);
        popScope(COMPILE_SCOPE_FOREACH);
        patchJump(iterDone, -1, NULL);
        patchBreaks(beforeForeach, currentIseq()->tail, 0);
        emitOp0(OP_POP); // pop the iterator value
        emitOp0(OP_POP); // pop the iterator
        vec_deinit(&v_slots);
        break;
    }
    case BREAK_STMT: {
        if (curLoopType == LOOP_T_NONE) {
            error("'break' can only be used in loops ('while', 'for', 'foreach' loops) and blocks");
            return;
        }
        if (curLoopType == LOOP_T_BLOCK) {
            emitOp0(OP_BLOCK_BREAK);
        } else {
            emitBreak();
        }
        break; // I heard you like break statements, so I put a break in your break
    }
    case CONTINUE_STMT: {
        if (curLoopType == LOOP_T_NONE) {
            error("'continue' can only be used in loops ('while', 'for', 'foreach' loops) and blocks");
            return;
        }
        if (curLoopType == LOOP_T_BLOCK) {
            if (current->function->chunk->count == 0) {
                emitOp0(OP_NIL);
                emitOp0(OP_POP);
            }
            emitOp0(OP_BLOCK_CONTINUE);
        } else if (curLoopType == LOOP_T_FOREACH) {
            Insn *in = emitJump(OP_JUMP);
            in->flags = INSN_FL_CONTINUE;
            in->extra = current->localCount-loopLocalCount;
        } else {
            ASSERT(curLoopType == LOOP_T_WHILE || curLoopType == LOOP_T_FOR);
            emitLoop(loopStart); // WHILE or FOR
        }
        break;
    }
    case PRINT_STMT: {
        emitChildren(n);
        emitOp0(OP_PRINT);
        break;
    }
    // single or multi-variable assignment, global or local
    case VAR_STMT: {
        int numVarsSet = 1;
        Node *lastNode = NULL;
        if (n->children && n->children->length > 0) {
            lastNode = vec_last(n->children);
        }
        bool uninitialized = lastNode == NULL || nodeKind(lastNode) == VAR_STMT;
        if (!uninitialized && n->children->length > 1) {
            numVarsSet += n->children->length - 1;
        }

        if (!uninitialized) {
            emitNode(lastNode); // expression node
        } else {
            for (int i = 0; i < numVarsSet; i++) {
                emitNil();
            }
        }

        if (numVarsSet > 1 && current->scopeDepth > 0) {
            pushVarSlots(); // for array
        }

        bytecode_t slotIdx = 0;
        for (int i = 0; i < numVarsSet; i++) {
            Node *varNode = NULL;
            if (i == 0) {
                varNode = n;
            } else {
                varNode = n->children->data[i-1];
            }
            int arg = declareVariable(&varNode->tok);
            if (arg == -1) return; // error already printed
            if (current->scopeDepth == 0) {
                if (numVarsSet == 1 || uninitialized) {
                    emitOp1(OP_DEFINE_GLOBAL, (bytecode_t)arg);
                } else {
                    emitOp2(OP_UNPACK_DEFINE_GLOBAL, (bytecode_t)arg, slotIdx);
                    slotIdx++;
                }
            } else {
                if (numVarsSet == 1 || uninitialized) {
                    emitOp2(OP_SET_LOCAL, (bytecode_t)arg, identifierConstant(&varNode->tok));
                } else {
                    emitOp3(OP_UNPACK_SET_LOCAL, (bytecode_t)arg, slotIdx, identifierConstant(&varNode->tok));
                    slotIdx++;
                }
            }
        }

        return;
    }
    case VARIABLE_EXPR: {
        namedVariable(n->tok, VAR_GET);
        break;
    }
    case CONSTANT_EXPR: {
        bytecode_t arg = identifierConstant(&n->tok);
        emitOp1(OP_GET_CONST, arg);
        break;
    }
    case CONSTANT_LOOKUP_EXPR: {
        if (n->children->length > 0) {
            emitChildren(n); // 1 child, the class/module
        }  else {
            emitOp0(OP_NIL); // resolve from top-level
        }
        bytecode_t arg = identifierConstant(&n->tok);
        emitOp1(OP_GET_CONST_UNDER, arg);
        break;
    }
    case ASSIGN_EXPR: {
        emitNode(n->children->data[1]); // rval
        Node *varNode = vec_first(n->children);
        if (varNode->type.kind == CONSTANT_EXPR) {
            bytecode_t arg = identifierConstant(&varNode->tok);
            emitOp1(OP_SET_CONST, arg);
        } else {
            namedVariable(varNode->tok, VAR_SET);
        }
        break;
    }
    case BLOCK_STMT: {
        ASSERT(n->children);
        pushScope(COMPILE_SCOPE_BLOCK);
        emitChildren(n); // 1 child, list of statements
        popScope(COMPILE_SCOPE_BLOCK);
        break;
    }
    case FUNCTION_STMT: {
        emitFunction(n, FUN_TYPE_NAMED);
        break;
    }
    case METHOD_STMT: {
        if (currentClassOrModule == NULL && !inINBlock) {
            error("Methods can only be declared in classes and modules. Maybe forgot keyword 'fun'?");
        } else {
            FunctionType ftype = FUN_TYPE_METHOD;
            if (strcmp(tokStr(&n->tok), "init") == 0) {
                ftype = FUN_TYPE_INIT;
            }
            emitFunction(n, ftype);
        }
        break;
    }
    case CLASS_METHOD_STMT: {
        if (currentClassOrModule == NULL && !inINBlock) {
            error("Static methods can only be declared in classes and modules.");
        } else {
            FunctionType ftype = FUN_TYPE_CLASS_METHOD;
            emitFunction(n, ftype);
        }
        break;
    }
    case GETTER_STMT: {
        if (currentClassOrModule == NULL && !inINBlock) {
            error("Getter methods can only be declared in classes and modules");
        } else {
            emitFunction(n, FUN_TYPE_GETTER);
        }
        break;
    }
    case SETTER_STMT: {
        if (currentClassOrModule == NULL && !inINBlock) {
            error("Setter methods can only be declared in classes and modules");
        } else {
            emitFunction(n, FUN_TYPE_SETTER);
        }
        break;
    }
    case ANON_FN_EXPR: {
        emitFunction(n, FUN_TYPE_ANON);
        break;
    }
    case CLASS_STMT: {
        emitClass(n);
        break;
    }
    case MODULE_STMT: {
        emitModule(n);
        break;
    }
    case IN_STMT: {
        emitIn(n);
        break;
    }
    case PROP_ACCESS_EXPR: {
        emitChildren(n);
        emitOp1(OP_PROP_GET, identifierConstant(&n->tok));
        break;
    }
    case PROP_SET_EXPR: {
        emitChildren(n);
        emitOp1(OP_PROP_SET, identifierConstant(&n->tok));
        break;
    }
    // ex: obj.prop = 1
    // (propGet (var o) prop)
    case PROP_SET_BINOP_EXPR: {
        emitNode(n->children->data[0]->children->data[0]);
        emitNode(n->children->data[0]);
        emitNode(n->children->data[1]);
        emitBinaryOp(n->tok);
        emitOp1(OP_PROP_SET, identifierConstant(&n->children->data[0]->tok));
        break;
    }
    case RETURN_STMT: {
        if (n->children->length > 0) {
            // always return instance in init function
            if (current->type == FUN_TYPE_INIT) {
                emitOp0(OP_GET_THIS);
            } else {
                emitChildren(n);
            }
            if (breakBlock) {
                ASSERT(current->type == FUN_TYPE_BLOCK); // TODO: error, don't assert
                emitOp0(OP_BLOCK_RETURN);
            } else {
                emitOp0(OP_RETURN);
            }
            COMP_TRACE("Emitting explicit return (children)");
        } else {
            COMP_TRACE("Emitting explicit return (void)");
            if (breakBlock) {
                emitOp0(OP_NIL);
                emitOp0(OP_BLOCK_RETURN);
            } else {
                if (current->type == FUN_TYPE_BLOCK) {
                    emitOp0(OP_BLOCK_CONTINUE);
                } else {
                    emitReturn(current);
                }
            }
        }
        break;
    }
    case THIS_EXPR: {
        emitOp0(OP_GET_THIS);
        break;
    }
    case SUPER_EXPR: {
        Node *tokNode = vec_last(n->children);
        bytecode_t methodNameArg = identifierConstant(&tokNode->tok);
        emitOp1(OP_GET_SUPER, methodNameArg);
        break;
    }
    case CALL_EXPR: {
        emitCall(n);
        break;
    }
    case CALL_BLOCK_EXPR: {
        CallInfo *cinfo = emitCall(n->children->data[0]);
        ObjFunction *block = emitFunction(n->children->data[1], FUN_TYPE_BLOCK);
        cinfo->blockFunction = block;
        break;
    }
    case TO_BLOCK_EXPR: {
        emitChildren(n);
        emitOp0(OP_TO_BLOCK);
        break;
    }
    case SPLAT_EXPR: {
        emitChildren(n);
        emitOp0(OP_SPLAT_ARRAY);
        break;
    }
    case KWARG_IN_CALL_STMT: {
        emitChildren(n);
        break;
    }
    case TRY_STMT: {
        Iseq *iseq = currentIseq();
        vec_void_t vjumps;
        vec_init(&vjumps);
        int ifrom = iseq->wordCount; // start of try block
        emitNode(n->children->data[0]); // try block
        bool skipJumpToEnd = false;
        if (n->children->length == 2) {
            Node *onlyChild = n->children->data[1];
            if (onlyChild->type.kind == ENSURE_STMT) {
                skipJumpToEnd = true;
            }
        }
        Insn *jumpToEnd = NULL;
        if (!skipJumpToEnd) {
            jumpToEnd = emitJump(OP_JUMP);
            vec_push(&vjumps, jumpToEnd);
        }
        int ito = iseq->wordCount; // end of try { } block, start of catch or ensure
        Node *catchStmt = NULL; int i = 0;
        Node *ensureStmt = NULL;
        Node *tryElseStmt = NULL;
        if (n->children->length > 1) {
            vec_foreach(n->children, catchStmt, i) {
                if (i == 0) continue; // already emitted
                int itarget = iseq->wordCount; // start of this catch block
                if (catchStmt->type.kind == CATCH_STMT) {
                    Node *catchConstant = vec_first(catchStmt->children);
                    ObjString *className = hiddenString("", 0, NEWOBJ_FLAG_NONE);
                    // catch with variables
                    if (catchConstant->children && catchConstant->children->length > 0) {
                        while (catchConstant->children && catchConstant->children->length > 0) {
                            Node *catchPrefix = vec_first(catchConstant->children);
                            Token classPrefixTok = catchPrefix->tok;
                            pushCString(className, tokStr(&classPrefixTok), strlen(tokStr(&classPrefixTok)));
                            pushCString(className, "::", 2);
                            Node *classSuffix = catchConstant;
                            Token classSuffixTok = classSuffix->tok;
                            pushCString(className, tokStr(&classSuffixTok), strlen(tokStr(&classSuffixTok)));
                            catchConstant = catchPrefix;
                        }
                        // catch just with class name
                    } else {
                        Token classTok = catchConstant->tok;
                        pushCString(className, tokStr(&classTok), strlen(tokStr(&classTok)));
                    }
                    /*fprintf(stderr, "classTok: %s, children: %d\n", tokStr(&classTok), catchStmt->children->length);*/
                    STRING_SET_STATIC(className);
                    double catchTblRowIdx = (double)iseqAddCatchRow(
                            iseq, ifrom, ito,
                            itarget, OBJ_VAL(className)
                            );
                    pushScope(COMPILE_SCOPE_TRY);
                    // given variable expression to bind to (Ex: (catch Error err))
                    if (catchStmt->children->length > 2) {
                        bytecode_t getThrownArg = makeConstant(NUMBER_VAL(catchTblRowIdx), CONST_T_NUMLIT);
                        emitOp1(OP_GET_THROWN, getThrownArg);
                        Token varTok = catchStmt->children->data[1]->tok;
                        declareVariable(&varTok);
                        namedVariable(varTok, VAR_SET);
                    }
                    emitNode(vec_last(catchStmt->children)); // catch { } block
                    ASSERT(iseq == currentIseq());
                    /*if (catchStmt->children->length > 2) {*/
                    /*emitOp0(OP_POP); // pop the bound error variable*/
                    /*}*/
                    // don't emit a jump at the end of the final catch statement, or if
                    // the next child after this one is an ensure block
                    if (i < n->children->length-1) {
                        Node *nextNode = n->children->data[i+1];
                        if (nextNode->type.kind == CATCH_STMT || nextNode->type.kind == TRY_ELSE_STMT) {
                            Insn *jumpStart = emitJump(OP_JUMP); // jump to end of try statement
                            vec_push(&vjumps, jumpStart);
                        }
                    }
                    popScope(COMPILE_SCOPE_TRY);
                } else { // ensure or else stmt
                    if (catchStmt->type.kind == TRY_ELSE_STMT) {
                        ASSERT(jumpToEnd);
                        vec_splice(&vjumps, 0, 1);
                        patchJump(jumpToEnd, -1, currentIseq()->tail);
                        jumpToEnd = NULL;
                        tryElseStmt = catchStmt;
                        ASSERT(tryElseStmt->type.kind == TRY_ELSE_STMT);
                        pushScope(COMPILE_SCOPE_TRY);
                        emitNode(vec_last(tryElseStmt->children)); // else block
                        popScope(COMPILE_SCOPE_TRY);
                    } else { // ensure
                        ensureStmt = catchStmt;
                        ASSERT(ensureStmt->type.kind == ENSURE_STMT);
                        Insn *jump = NULL; int j = 0;
                        vec_foreach(&vjumps, jump, j) {
                            patchJump(jump, -1, currentIseq()->tail);
                        }
                        vec_clear(&vjumps);
                        double catchTblRowIdx = iseqAddEnsureRow(iseq, ifrom, ito, itarget);
                        bytecode_t rethrowIfArg = makeConstant(NUMBER_VAL(catchTblRowIdx), CONST_T_NUMLIT);
                        pushScope(COMPILE_SCOPE_TRY);
                        emitNode(vec_last(ensureStmt->children)); // ensure block
                        emitOp1(OP_RETHROW_IF_ERR, rethrowIfArg);
                        popScope(COMPILE_SCOPE_TRY);
                    }
                }
            }

            Insn *jump = NULL; int j = 0;
            vec_foreach(&vjumps, jump, j) {
                patchJump(jump, -1, currentIseq()->tail);
            }
        }
        vec_deinit(&vjumps);
        break;
    }
    case THROW_STMT: {
        emitChildren(n);
        emitOp0(OP_THROW);
        break;
    }
    case INDEX_GET_EXPR: {
        emitChildren(n);
        emitOp0(OP_INDEX_GET);
        break;
    }
    case INDEX_SET_EXPR: {
        emitChildren(n);
        emitOp0(OP_INDEX_SET);
        break;
    }
    default:
        UNREACHABLE("invalid (unknown) node. kind (%d) not implemented (tok=%s)",
            nodeKind(n), tokStr(&n->tok)
        );
    }
}

static void initializeCompiler(void) {
    current = NULL;
    top = NULL; // compiler for main
    currentClassOrModule = NULL;
    inINBlock = false;
    curTok = NULL;
    loopStart = -1;
    curLoopType = LOOP_T_NONE;
    nodeDepth = 0;
    nodeWidth = -1;
    blockDepth = 0;
    breakBlock = false;
    curScope = NULL;
    loopLocalCount = 0;
    nextInsnIsLabel = false;
}

ObjFunction *compile_src(char *src, CompileErr *err) {
    initializeCompiler();
    initScanner(&scanner, src);
    Parser p;
    initParser(&p);
    Node *program = parse(&p);
    if (CLOX_OPTION_T(parseOnly)) {
        *err = p.hadError ? COMPILE_ERR_SYNTAX :
            COMPILE_ERR_NONE;
        if (p.hadError) {
            outputParserErrors(&p, stderr);
            freeParser(&p);
            return NULL;
        }
        return 0;
    } else if (p.hadError) {
        outputParserErrors(&p, stderr);
        freeParser(&p); // TODO: throw SyntaxError
        *err = COMPILE_ERR_SYNTAX;
        return NULL;
    }
    ASSERT(program);
    freeParser(&p);
    Compiler mainCompiler;
    top = &mainCompiler;
    initCompiler(&mainCompiler, 0, FUN_TYPE_TOP_LEVEL, NULL, NULL);
    emitNode(program);
    ObjFunction *prog = endCompiler();
    prog->programNode = program;
    if (CLOX_OPTION_T(debugBytecode) && !mainCompiler.hadError) {
        printFunctionTables(stderr, prog);
        printDisassembledChunk(stderr, prog->chunk, "Bytecode:");
    }
    if (mainCompiler.hadError) {
        outputCompilerErrors(&mainCompiler, stderr);
        freeCompiler(&mainCompiler, true);
        *err = COMPILE_ERR_SEMANTICS;
        return NULL;
    } else {
        *err = COMPILE_ERR_NONE;
        ASSERT(prog->chunk);
        return prog;
    }
}

ObjFunction *compile_node(Node *program, CompileErr *err) {
    initializeCompiler();
    Compiler mainCompiler;
    top = &mainCompiler;
    initCompiler(&mainCompiler, 0, FUN_TYPE_TOP_LEVEL, NULL, NULL);
    emitNode(program);
    ObjFunction *prog = endCompiler();
    prog->programNode = program;
    if (CLOX_OPTION_T(debugBytecode) && !mainCompiler.hadError) {
        printFunctionTables(stderr, prog);
        printDisassembledChunk(stderr, prog->chunk, "Bytecode:");
    }
    if (mainCompiler.hadError) {
        outputCompilerErrors(&mainCompiler, stderr);
        freeCompiler(&mainCompiler, true);
        *err = COMPILE_ERR_SEMANTICS;
        return NULL;
    } else {
        *err = COMPILE_ERR_NONE;
        ASSERT(prog->chunk);
        return prog;
    }
}

ObjFunction *compile_eval_src(char *src, CompileErr *err, ObjInstance *instance, ObjFunction *func_in, bytecode_t *ip_at) {
    initScanner(&scanner, src);
    Parser p;
    initParser(&p);
    Node *program;
    bool compilingClassBody = false;
    if (instance && TO_OBJ(instance)->type == OBJ_T_CLASS) {
        program = parseClass(&p);
        compilingClassBody = true;
    } else {
        program = parse(&p);
    }
    if (CLOX_OPTION_T(parseOnly)) {
        *err = p.hadError ? COMPILE_ERR_SYNTAX :
            COMPILE_ERR_NONE;
        if (p.hadError) {
            outputParserErrors(&p, stderr);
            freeParser(&p);
            return NULL;
        }
        return 0;
    } else if (p.hadError) {
        outputParserErrors(&p, stderr);
        freeParser(&p); // TODO: throw SyntaxError
        *err = COMPILE_ERR_SYNTAX;
        return NULL;
    }
    ASSERT(program);
    freeParser(&p);
    Compiler mainCompiler;
    top = &mainCompiler;
    ClassCompiler classCompiler;
    initEvalCompiler(&mainCompiler, func_in, ip_at);
    if (compilingClassBody) {
        currentClassOrModule = &classCompiler;
        classCompiler.name = syntheticToken(instance->klass->classInfo->name->chars);
        classCompiler.hasSuperclass = false;
        classCompiler.isModule = false;
        classCompiler.enclosing = NULL;
    }
    CompileScopeType stype = COMPILE_SCOPE_FUNCTION;
    if (func_in->ftype == FUN_TYPE_TOP_LEVEL) {
      stype = COMPILE_SCOPE_MAIN;
    }
    pushScope(stype);
    emitNode(program);
    popScope(stype);
    ObjFunction *prog = endCompiler();
    prog->programNode = program;
    if (CLOX_OPTION_T(debugBytecode) && !mainCompiler.hadError) {
        printFunctionTables(stderr, prog);
        printDisassembledChunk(stderr, prog->chunk, "Bytecode:");
    }
    if (mainCompiler.hadError) {
        outputCompilerErrors(&mainCompiler, stderr);
        freeCompiler(&mainCompiler, true);
        *err = COMPILE_ERR_SEMANTICS;
        return NULL;
    } else {
        *err = COMPILE_ERR_NONE;
        ASSERT(prog->chunk);
        return prog;
    }
}

ObjFunction *compile_binding_eval_src(char *src, CompileErr *err, ObjScope *scope) {
    initScanner(&scanner, src);
    Parser p;
    initParser(&p);
    Node *program = parse(&p);
    if (CLOX_OPTION_T(parseOnly)) {
        *err = p.hadError ? COMPILE_ERR_SYNTAX :
            COMPILE_ERR_NONE;
        if (p.hadError) {
            outputParserErrors(&p, stderr);
            freeParser(&p);
            return NULL;
        }
        return 0;
    } else if (p.hadError) {
        outputParserErrors(&p, stderr);
        freeParser(&p); // TODO: throw SyntaxError
        *err = COMPILE_ERR_SYNTAX;
        return NULL;
    }
    ASSERT(program);
    freeParser(&p);
    Compiler mainCompiler;
    top = &mainCompiler;
    initBindingEvalCompiler(&mainCompiler, scope);
    CompileScopeType stype = COMPILE_SCOPE_FUNCTION;
    if (scope->function->ftype == FUN_TYPE_TOP_LEVEL) {
      stype = COMPILE_SCOPE_MAIN;
    }
    pushScope(stype);
    emitNode(program);
    popScope(stype);
    ObjFunction *prog = endCompiler();
    prog->programNode = program;
    if (CLOX_OPTION_T(debugBytecode) && !mainCompiler.hadError) {
        printFunctionTables(stderr, prog);
        printDisassembledChunk(stderr, prog->chunk, "Bytecode:");
    }
    if (mainCompiler.hadError) {
        outputCompilerErrors(&mainCompiler, stderr);
        freeCompiler(&mainCompiler, true);
        *err = COMPILE_ERR_SEMANTICS;
        return NULL;
    } else {
        *err = COMPILE_ERR_NONE;
        ASSERT(prog->chunk);
        return prog;
    }
}

ObjFunction *compile_file(char *fname, CompileErr *err) {
    int fd = open(fname, O_RDONLY);
    if (fd == -1) {
        *err = COMPILE_ERR_ERRNO;
        return NULL;
    }
    struct stat st;
    int res = fstat(fd, &st);
    if (res != 0) {
        *err = COMPILE_ERR_ERRNO;
        return NULL;
    }
    // TODO: free this buf or have function own it
    char *buf = xcalloc(1, st.st_size+1);
    ASSERT_MEM(buf);
    res = (int)read(fd, buf, st.st_size);
    if (res == -1) {
        *err = COMPILE_ERR_ERRNO;
        xfree(buf);
        return NULL;
    }
    ObjFunction *func = compile_src(buf, err);
    return func;
}

void grayCompilerRoots(void) {
    Compiler *compiler = current;
    while (compiler != NULL) {
        grayObject(TO_OBJ(compiler->function));
        compiler = compiler->enclosing;
    }
}
