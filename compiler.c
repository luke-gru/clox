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

typedef struct Local {
  // The name of the local variable.
  Token name;

  // The depth in the scope chain that this variable was declared at. Zero is
  // the outermost scope--parameters for a method, or the first local block in
  // top level code. One is the scope within that, etc.
  int depth;
  bool isUpvalue;
} Local;

typedef struct Upvalue {
  // The index of the local variable or upvalue being captured from the
  // enclosing function.
  uint8_t index;

  // Whether the captured variable is a local or upvalue in the enclosing
  // function.
  bool isLocal; // is local variable in immediately enclosing scope
} Upvalue;

struct Compiler; // fwd decl

typedef struct Compiler {
  // The currently in scope local variables.
  struct Compiler *enclosing;
  ObjFunction *function; // function or top-level code object
  FunctionType type;
  Local locals[256];
  Upvalue upvalues[256];

  // The number of local variables declared/defined in this scope (including
  // function parameters).
  int localCount;

  // The current level of block scope nesting. Zero is the outermost local
  // scope (global scope)
  int scopeDepth;
  bool hadError;

  Iseq iseq; // Generated instructions for the function
  Table constTbl;
} Compiler;


typedef struct ClassCompiler {
  struct ClassCompiler *enclosing;
  Token name;
  bool hasSuperclass;
  bool isModule;
} ClassCompiler;


typedef enum {
    COMPILE_SCOPE_BLOCK = 1,
    COMPILE_SCOPE_FUNCTION,
    COMPILE_SCOPE_CLASS,
    COMPILE_SCOPE_IN,
    COMPILE_SCOPE_MODULE, // TODO
} CompileScopeType;

static const char *compileScopeName(CompileScopeType stype) {
    switch (stype) {
    case COMPILE_SCOPE_BLOCK: return "SCOPE_BLOCK";
    case COMPILE_SCOPE_FUNCTION: return "SCOPE_FUNCTION";
    case COMPILE_SCOPE_CLASS: return "SCOPE_CLASS";
    case COMPILE_SCOPE_IN: return "SCOPE_IN";
    case COMPILE_SCOPE_MODULE: return "SCOPE_MODULE";
    default: {
        UNREACHABLE("invalid scope type: %d", stype);
    }
    }
}

static Compiler *current = NULL;
static Compiler *top = NULL;
static ClassCompiler *currentClassOrModule = NULL;
static bool inINBlock = false;
static Token *curTok = NULL;
static int loopStart = -1;
static int nodeDepth = 0;
static int nodeWidth = -1;

CompilerOpts compilerOpts; // [external]

typedef enum {
    VAR_GET = 1,
    VAR_SET,
} VarOp;

typedef enum {
    CONST_T_NUMLIT = 1,
    CONST_T_STRLIT,
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

static void error(const char *format, ...) {
    va_list args;
    int line = curTok ? curTok->line : 0;
    va_start(args, format);
    fprintf(stderr, "[Compile Error]: ");
    if (line > 0) {
        fprintf(stderr, "(line: %d) ", line);
    }
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    current->hadError = true;
    top->hadError = true;
}

static Token syntheticToken(const char *lexeme) {
    Token tok;
    tok.start = lexeme;
    tok.length = strlen(lexeme);
    tok.lexeme = lexeme;
    return tok;
}

static Chunk *currentChunk() {
  return &current->function->chunk;
}

static Iseq *currentIseq() {
    return &current->iseq;
}

static Insn *emitInsn(Insn in) {
    COMP_TRACE("emitInsn: %s", opName(in.code));
    in.lineno = curTok ? curTok->line : 0;
    in.nlvl.depth = nodeDepth;
    in.nlvl.width = nodeWidth;
    Insn *inHeap = calloc(sizeof(in), 1);
    memcpy(inHeap, &in, sizeof(in));
    iseqAddInsn(currentIseq(), inHeap);
    return inHeap;
}

static Insn *emitOp0(uint8_t code) {
    Insn in;
    memset(&in, 0, sizeof(in));
    in.code = code;
    in.numOperands = 0;
    in.flags = 0;
    return emitInsn(in);
}
static Insn *emitOp1(uint8_t code, uint8_t op1) {
    Insn in;
    memset(&in, 0, sizeof(in));
    in.code = code;
    in.operands[0] = op1;
    in.numOperands = 1;
    in.flags = 0;
    return emitInsn(in);
}
static Insn *emitOp2(uint8_t code, uint8_t op1, uint8_t op2) {
    Insn in;
    memset(&in, 0, sizeof(in));
    in.code = code;
    in.operands[0] = op1;
    in.operands[1] = op2;
    in.numOperands = 2;
    in.flags = 0;
    return emitInsn(in);
}
static Insn *emitOp3(uint8_t code, uint8_t op1, uint8_t op2, uint8_t op3) {
    Insn in;
    memset(&in, 0, sizeof(in));
    in.code = code;
    in.operands[0] = op1;
    in.operands[1] = op2;
    in.operands[2] = op3;
    in.numOperands = 3;
    in.flags = 0;
    return emitInsn(in);
}

// blocks (`{}`) push new scopes
static void pushScope(CompileScopeType stype) {
    current->scopeDepth++;
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
        emitOp0(OP_NIL);
        emitOp0(OP_RETURN);
    }
}

static void emitCloseUpvalue(void) {
    COMP_TRACE("Emitting close upvalue");
    emitOp0(OP_CLOSE_UPVALUE);
}

static void popScope(CompileScopeType stype) {
    COMP_TRACE("popScope: %s (depth=%d)", compileScopeName(stype), current->scopeDepth);
    while (current->localCount > 0 && current->locals[current->localCount - 1].depth >= current->scopeDepth) {
        if (current->locals[current->localCount - 1].isUpvalue) {
            COMP_TRACE("popScope closing upvalue");
            emitCloseUpvalue();
        } else {
            COMP_TRACE("popScope emitting OP_POP");
            emitOp0(OP_POP);
        }
        current->localCount--;
    }
    if (stype == COMPILE_SCOPE_FUNCTION) {
        emitReturn(current);
    }
    current->scopeDepth--;
}

static Insn *emitBytes(uint8_t code, uint8_t op) {
    return emitOp1(code, op);
}

static void emitNil() {
    emitOp0(OP_NIL);
}

// exit from script
static void emitLeave() {
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

    if (compiler->function->upvalueCount == 256) {
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
    return memcmp(a->start, b->start, a->length) == 0;
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

static void writeChunkByte(Chunk *chunk, uint8_t byte, int lineno, int nDepth, int nWidth) {
    writeChunk(chunk, byte, lineno, nDepth, nWidth);
}

static bool isBinOp(Insn *in) {
    uint8_t code = in->code;
    switch (code) {
        case OP_ADD:
        case OP_SUBTRACT:
        case OP_MULTIPLY:
        case OP_DIVIDE:
            return true;
        default:
            return false;
    }
}

static bool isNumConstOp(Insn *in) {
    return in->code == OP_CONSTANT && ((in->flags & INSN_FL_NUMBER) != 0);
}

static Value iseqGetConstant(Iseq *seq, uint8_t idx) {
    return seq->constants.values[idx];
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
        default:
            UNREACHABLE("bug");
    }
}

static void changeConstant(Iseq *seq, uint8_t constIdx, Value newVal) {
    ASSERT(constIdx < seq->constants.count);
    seq->constants.values[constIdx] = newVal;
}

static bool isJump(Insn *in) {
    switch (in->code) {
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_TRUE_PEEK:
        case OP_JUMP_IF_FALSE_PEEK:
            return true;
        default:
            return false;
    }
}

static Insn *jumpToInsn(Insn *in) {
    return in->jumpTo;
}

static bool isJumpNextInsn(Insn *in) {
    return in->operands[0] == 0;
}

static bool isJumpOrLoop(Insn *in) {
    return isJump(in) || in->code == OP_LOOP;
}

static void rmInsnAndPatchLabels(Iseq *seq, Insn *insn) {
    if (!insn->isLabel) {
        COMP_TRACE("Removing non-label instruction %s", opName(insn->code));
        iseqRmInsn(seq, insn);
        return;
    }
    int numBytes = insn->numOperands+1;
    Insn *in = seq->insns;
    int numLabelsPatched = 0;
    while (in) {
        if (in == insn) {
            in = in->next;
            continue;
        }
        if (isJumpOrLoop(in) && in->jumpTo == insn) {
            numLabelsPatched++;
            in->jumpTo = insn->next;
            insn->next->isLabel = true;
            in->operands[0] -= numBytes;
        }
        in = in->next;
    }
    ASSERT(numLabelsPatched > 0);
    COMP_TRACE("Removing label instruction %s after patching %d labels", opName(insn->code), numLabelsPatched);
    iseqRmInsn(seq, insn);
}

static int replaceJumpInsn(Iseq *seq, Insn *jumpInsn) {
    switch (jumpInsn->code) {
        case OP_JUMP:
        case OP_JUMP_IF_FALSE_PEEK:
        case OP_JUMP_IF_TRUE_PEEK:
            if (!jumpInsn->isLabel) {
                rmInsnAndPatchLabels(seq, jumpInsn);
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

static bool isJumpIfFalse(Insn *insn) {
    return insn->code == OP_JUMP_IF_FALSE;
}

static bool isJumpIfTrue(Insn *insn) {
    return insn->code == OP_JUMP_IF_TRUE_PEEK;
}

static bool isPop(Insn *insns) {
    return insns->code == OP_POP;
}

static void optimizeIseq(Iseq *iseq) {
    COMP_TRACE("OptimizeIseq");
    Insn *cur = iseq->insns; // first insn
    Insn *prev = NULL;
    Insn *prevp = NULL;
    int idx = 0;
    while (cur) {
        COMP_TRACE("optimize idx %d", idx);
        prev = cur->prev;
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
                    iseqRmInsn(iseq, cur);
                    iseqRmInsn(iseq, prev);
                    cur = prevp;
                    idx -= 2;
                    continue;
                }
            }
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
        if (isJump(cur) && isConst(prev)) {
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
            if (isPop(cur) && noSideEffectsConst(prev)) {
                COMP_TRACE("removing side effect expr 1");
                rmInsnAndPatchLabels(iseq, prev);
                COMP_TRACE("removing side effect expr 2");
                rmInsnAndPatchLabels(iseq, cur);
                cur = iseq->insns;
                idx = 0;
                continue;
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
    COMP_TRACE("copyIseqToChunk (%d insns, bytecount: %d)", iseq->count, iseq->byteCount);
    chunk->catchTbl = iseq->catchTbl;
    chunk->constants = iseq->constants;
    Insn *in = iseq->insns;
    int idx = 0;
    while (in) {
        idx++;
        writeChunkByte(chunk, in->code, in->lineno, in->nlvl.depth, in->nlvl.width);
        for (int i = 0; i < in->numOperands; i++) {
            writeChunkByte(chunk, in->operands[i], in->lineno, in->nlvl.depth, in->nlvl.width);
        }
        in = in->next;
    }
    ASSERT(idx == iseq->count);
    COMP_TRACE("/copyIseqToChunk");
}

static ObjFunction *endCompiler() {
    COMP_TRACE("endCompiler");
    if (current->type == FUN_TYPE_TOP_LEVEL) {
        emitLeave();
    }
    ObjFunction *func = current->function;
    copyIseqToChunk(currentIseq(), currentChunk());
    freeTable(&current->constTbl);
    freeIseq(&current->iseq);

    current = current->enclosing;
    COMP_TRACE("/endCompiler");
    return func;
}

// Adds a constant to the current instruction sequence's constant pool
// and returns an index to it.
static uint8_t makeConstant(Value value, ConstType ctype) {
    Value existingIdx;
    bool canMemoize = ctype == CONST_T_STRLIT;
    if (canMemoize) {
        if (tableGet(&current->constTbl, value, &existingIdx)) {
            return (uint8_t)AS_NUMBER(existingIdx);
        }
    }
    int constant = iseqAddConstant(currentIseq(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }
    if (canMemoize) {
        ASSERT(
            tableSet(&current->constTbl, value, NUMBER_VAL(constant))
        );
    }
    return (uint8_t)constant;
}

// Add constant to constant pool from the token's lexeme, return index to it
static uint8_t identifierConstant(Token* name) {
    DBG_ASSERT(vm.inited);
    return makeConstant(OBJ_VAL(hiddenString(name->start, name->length)),
        CONST_T_STRLIT);
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
    vec_foreach(n->children, stmt, i) {
        emitNode(stmt);
    }
    nodeDepth--;
    nodeWidth = lastWidth;
}

// emit a jump (forwards) instruction, returns a pointer to the byte that needs patching
static Insn *emitJump(OpCode jumpOp) {
    return emitOp1(jumpOp, 0); // patched later
}

static int insnOffset(Insn *start, Insn *end) {
    ASSERT(start);
    ASSERT(end);
    int offset = 0;
    Insn *cur = start;
    while (cur && cur != end) {
        offset += (cur->numOperands+1);
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

// Emit a jump backwards (loop) instruction from the current code count to offset `loopStart`
// TODO: make the offset bigger than 1 byte!
static void emitLoop(int loopStart) {

  int offset = (currentIseq()->byteCount - loopStart)+2;
  if (offset > UINT8_MAX) error("Loop body too large.");
  ASSERT(offset >= 0);

  emitOp1(OP_LOOP, offset);
}

static bool isBreak(Insn *in) {
    return (in->code == OP_JUMP &&
            (in->flags & INSN_FL_BREAK) != 0);
}

static void patchBreaks(Insn *start, Insn *end) {
    Insn *cur = start;
    int numFound = 0;
    while (cur != end) {
        if (isBreak(cur) && cur->operands[0] == 0) {
            int offset = insnOffset(cur, end)+1;
            COMP_TRACE("jump offset found, patching break: %d", offset);
            patchJump(cur, offset, end);
            numFound++;
        }
        cur = cur->next;
    }
    COMP_TRACE("Patched %d breaks", numFound);
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
    };
    current->locals[current->localCount] = local;
    current->localCount++;
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
            Local* local = &current->locals[i];
            if (local->depth != -1 && local->depth < current->scopeDepth) break;
            if (identifiersEqual(name, &local->name)) {
                error("Variable with name '%s' already defined in this scope.", tokStr(name));
                return -1;
            }
        }
        return addLocal(*name);
    }
}


// emit GET/SET global or local for named variable
static void namedVariable(Token name, VarOp getSet) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(current, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }
  if (getSet == VAR_SET) {
    emitOp1(setOp, (uint8_t)arg);
  } else {
    emitOp1(getOp, (uint8_t)arg);
  }
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
    compiler->enclosing = current;
    compiler->localCount = 0; // NOTE: below, this is increased to 1
    compiler->scopeDepth = scopeDepth;
    compiler->function = newFunction(chunk, NULL);
    initIseq(&compiler->iseq);
    hideFromGC((Obj*)compiler->function); // TODO: figure out way to unhide these functions on freeVM()
    compiler->type = ftype;
    compiler->hadError = false;
    initTable(&compiler->constTbl);

    current = compiler;

    switch (ftype) {
    case FUN_TYPE_NAMED:
        current->function->name = hiddenString(
            tokStr(fTok), strlen(tokStr(fTok))
        );
        break;
    case FUN_TYPE_INIT:
    case FUN_TYPE_GETTER:
    case FUN_TYPE_SETTER:
    case FUN_TYPE_METHOD:
    case FUN_TYPE_CLASS_METHOD: {
        ASSERT(currentClassOrModule || inINBlock);
        char *className = "";
        if (currentClassOrModule) {
            className = tokStr(&currentClassOrModule->name);
        }
        char *funcName = tokStr(fTok);
        size_t methodNameBuflen = strlen(className)+1+strlen(funcName)+1; // +1 for '.' in between
        char *methodNameBuf = calloc(methodNameBuflen, 1);
        ASSERT_MEM(methodNameBuf);
        strcpy(methodNameBuf, className);
        char *sep = "#";
        if (ftype == FUN_TYPE_CLASS_METHOD) {
            sep = ".";
        }
        strncat(methodNameBuf, sep, 1);
        strcat(methodNameBuf, funcName);
        ObjString *methodName = hiddenString(methodNameBuf, strlen(methodNameBuf));
        current->function->name = methodName;
        free(methodNameBuf);
        break;
    }
    case FUN_TYPE_ANON:
    case FUN_TYPE_TOP_LEVEL:
        current->function->name = NULL;
        break;
    default:
        UNREACHABLE("invalid function type %d", ftype);
    }

    // The first local variable slot is always implicitly declared, unless
    // we're in the function representing the top-level (main).
    if (ftype != FUN_TYPE_TOP_LEVEL) {
        Local *local = &current->locals[current->localCount++];
        local->depth = current->scopeDepth;
        local->isUpvalue = false;
        if (ftype == FUN_TYPE_METHOD || ftype == FUN_TYPE_INIT ||
                ftype == FUN_TYPE_GETTER || ftype == FUN_TYPE_SETTER ||
                ftype == FUN_TYPE_CLASS_METHOD) {
            local->name.start = "";
            local->name.length = 0;
        } else {
            // In a function, it holds the function, but cannot be referenced, so has
            // no name.
            local->name.start = "";
            local->name.length = 0;
        }
    }
    COMP_TRACE("/initCompiler");
}

// Define a declared variable in local or global scope (locals MUST be
// declared before being defined)
static void defineVariable(uint8_t arg, bool checkDecl) {
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
    uint8_t nameConstant = identifierConstant(&n->tok);
    ClassCompiler cComp;
    memset(&cComp, 0, sizeof(cComp));
    cComp.name = n->tok;
    Token *superClassTok = (Token*)nodeGetData(n);
    cComp.hasSuperclass = superClassTok != NULL;
    cComp.isModule = false;
    cComp.enclosing = currentClassOrModule;
    currentClassOrModule = &cComp;

    pushScope(COMPILE_SCOPE_CLASS);
    // TODO: add 'this' as upvalue or local var in class
    if (cComp.hasSuperclass) {
        namedVariable(*superClassTok, VAR_GET);
        emitOp1(OP_SUBCLASS, nameConstant); // VM peeks the superclass and gets the class name
    } else {
        emitOp1(OP_CLASS, nameConstant); // VM gets the class name
    }

    emitChildren(n); // block node with methods and other declarations

    popScope(COMPILE_SCOPE_CLASS);

    // define the local or global variable for the class itself
    if (current->scopeDepth == 0) {
        defineVariable(nameConstant, false);
    } else {
        uint8_t defineArg = declareVariable(&n->tok);
        defineVariable(defineArg, true);
    }
    currentClassOrModule = cComp.enclosing;
}

static void emitModule(Node *n) {
    uint8_t nameConstant = identifierConstant(&n->tok);

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
    if (current->scopeDepth == 0) {
        defineVariable(nameConstant, false);
    } else {
        uint8_t defineArg = declareVariable(&n->tok);
        defineVariable(defineArg, true);
    }
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

// emit function or method
static void emitFunction(Node *n, FunctionType ftype) {
    Compiler fCompiler;
    initCompiler(&fCompiler, current->scopeDepth,
        ftype, &n->tok, NULL);
    pushScope(COMPILE_SCOPE_FUNCTION); // this scope holds the local variable parameters

    ObjFunction *func = fCompiler.function;
    func->funcNode = n;

    vec_nodep_t *params = (vec_nodep_t*)nodeGetData(n);
    ASSERT(params);
    Node *param = NULL; int i = 0;
    vec_foreach(params, param, i) {
        if (param->type.kind == PARAM_NODE_REGULAR) {
            uint8_t localSlot = declareVariable(&param->tok);
            defineVariable(localSlot, true);
            func->arity++;
        }
    }
    i = 0;
    // optional arguments gets pushed in order so we can skip the local var set code when necessary
    // (when the argument is given during the call).
    int numParams = params->length;
    vec_foreach(params, param, i) {
        if (param->type.kind == PARAM_NODE_DEFAULT_ARG) {
            uint8_t localSlot = declareVariable(&param->tok);
            defineVariable(localSlot, true);
            func->numDefaultArgs++;
            Insn *insnBefore = currentIseq()->tail;
            emitNode(vec_first(param->children)); // default arg
            emitOp1(OP_SET_LOCAL, (uint8_t)localSlot);
            Insn *insnAfter = currentIseq()->tail;
            size_t codeDiff = iseqInsnByteDiff(insnBefore, insnAfter);
            ParamNodeInfo *paramNodeInfo = calloc(sizeof(ParamNodeInfo), 1);
            ASSERT_MEM(paramNodeInfo);
            paramNodeInfo->defaultArgIPOffset = codeDiff;
            ASSERT(param->data == NULL);
            param->data = paramNodeInfo;
        } else if (param->type.kind == PARAM_NODE_SPLAT) {
            uint8_t localSlot = declareVariable(&param->tok);
            defineVariable(localSlot, true);
            func->hasRestArg = true;
        } else if (param->type.kind == PARAM_NODE_KWARG) {
            uint8_t localSlot = declareVariable(&param->tok);
            defineVariable(localSlot, true);
            emitOp2(OP_CHECK_KEYWORD,
                localSlot /* slot of keyword argument */,
                numParams+1 /* slot of keyword map */
            );
            func->numKwargs++;
            Insn *ifJumpStart = emitJump(OP_JUMP_IF_TRUE);
            emitChildren(param);
            emitOp1(OP_SET_LOCAL, localSlot);
            patchJump(ifJumpStart, -1, NULL);
        }
    }
    emitChildren(n); // the blockNode
    popScope(COMPILE_SCOPE_FUNCTION);
    func = endCompiler();

    // save the chunk as a constant in the parent (now current) chunk
    uint8_t funcIdx = makeConstant(OBJ_VAL(func), CONST_T_CODE);
    emitOp1(OP_CLOSURE, funcIdx);
    // Emit arguments for each upvalue to know whether to capture a local or
    // an upvalue.
    for (int i = 0; i < func->upvalueCount; i++) {
        emitOp0(fCompiler.upvalues[i].isLocal ? 1 : 0);
        emitOp0(fCompiler.upvalues[i].index);
    }

    if (ftype == FUN_TYPE_TOP_LEVEL) {
        return;
    }

    if (ftype != FUN_TYPE_ANON) {
        if ((currentClassOrModule == NULL && !inINBlock) || ftype == FUN_TYPE_NAMED) { // regular function
            namedVariable(n->tok, VAR_SET);
        } else { // method
            func->isMethod = true;
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
        return;
    }
    case EXPR_STMT: {
        emitChildren(n);
        emitOp0(OP_POP);
        return;
    }
    case BINARY_EXPR: {
        emitChildren(n);
        if (n->tok.type == TOKEN_PLUS) {
            emitOp0(OP_ADD);
        } else if (n->tok.type == TOKEN_MINUS) {
            emitOp0(OP_SUBTRACT);
        } else if (n->tok.type == TOKEN_STAR) {
            emitOp0(OP_MULTIPLY);
        } else if (n->tok.type == TOKEN_SLASH) {
            emitOp0(OP_DIVIDE);
        } else if (n->tok.type == TOKEN_LESS) {
            emitOp0(OP_LESS);
        } else if (n->tok.type == TOKEN_LESS_EQUAL) {
            emitOp0(OP_LESS_EQUAL);
        } else if (n->tok.type == TOKEN_GREATER) {
            emitOp0(OP_GREATER);
        } else if (n->tok.type == TOKEN_GREATER_EQUAL) {
            emitOp0(OP_GREATER_EQUAL);
        } else if (n->tok.type == TOKEN_EQUAL_EQUAL) {
            emitOp0(OP_EQUAL);
        } else {
            UNREACHABLE("invalid binary expr node (token: %s)", tokStr(&n->tok));
        }
        return;
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
        return;
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
        return;
    }
    case LITERAL_EXPR: {
        if (n->tok.type == TOKEN_NUMBER) {
            // TODO: handle strtod error condition
            double d = strtod(tokStr(&n->tok), NULL);
            emitConstant(NUMBER_VAL(d), CONST_T_NUMLIT);
        // non-static string
        } else if (n->tok.type == TOKEN_STRING_SQUOTE || n->tok.type == TOKEN_STRING_DQUOTE) {
            Token *name = &n->tok;
            ObjString *str = hiddenString(name->start+1, name->length-2);
            uint8_t strSlot = makeConstant(OBJ_VAL(str), CONST_T_STRLIT);
            emitOp2(OP_STRING, strSlot, 0);
        // static string
        } else if (n->tok.type == TOKEN_STRING_STATIC) {
            Token *name = &n->tok;
            ObjString *str = hiddenString(name->start+2, name->length-3);
            uint8_t strSlot = makeConstant(OBJ_VAL(str), CONST_T_STRLIT);
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
        return;
    }
    case ARRAY_EXPR: {
        Token arrayTok = syntheticToken("Array");
        namedVariable(arrayTok, VAR_GET);
        emitChildren(n);
        CallInfo *callInfoData = calloc(sizeof(CallInfo), 1);
        ASSERT_MEM(callInfoData);
        callInfoData->nameTok = arrayTok;
        callInfoData->argc = n->children->length;
        callInfoData->numKwargs = 0;
        ObjInternal *callInfoObj = newInternalObject(callInfoData, NULL, NULL);
        hideFromGC((Obj*)callInfoObj);
        uint8_t callInfoConstSlot = makeConstant(OBJ_VAL(callInfoObj), CONST_T_CALLINFO);
        emitOp2(OP_CALL, (uint8_t)n->children->length, callInfoConstSlot);
        return;
    }
    case IF_STMT: {
        emitNode(n->children->data[0]); // condition
        Insn *ifJumpStart = emitJump(OP_JUMP_IF_FALSE);
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
        break;
    }
    case WHILE_STMT: {
        int oldLoopStart = loopStart;
        Insn *loopLabel = currentIseq()->tail;
        loopStart = currentIseq()->byteCount + 2;
        emitNode(vec_first(n->children)); // cond
        if (loopLabel) {
            loopLabel = loopLabel->next; // beginning of conditional
        } else {
            loopLabel = currentIseq()->tail;
        }
        Insn *whileJumpStart = emitJump(OP_JUMP_IF_FALSE);
        whileJumpStart->isLabel = true;
        emitNode(n->children->data[1]); // while block
        loopLabel->jumpTo = whileJumpStart;
        emitLoop(loopStart);
        patchJump(whileJumpStart, -1, NULL);
        patchBreaks(whileJumpStart, currentIseq()->tail);
        loopStart = oldLoopStart;
        break;
    }
    case FOR_STMT: {
        pushScope(COMPILE_SCOPE_BLOCK);
        Node *init = vec_first(n->children);
        if (init) {
            emitNode(init);
        }
        int oldLoopStart = loopStart;
        Node *test = n->children->data[1];
        int beforeTest = currentIseq()->byteCount+2;
        loopStart = beforeTest;
        if (test) {
            emitNode(test);
        } else {
            emitOp0(OP_TRUE);
        }
        Insn *forJump = emitJump(OP_JUMP_IF_FALSE);
        Node *forBlock = vec_last(n->children);
        emitNode(forBlock);
        Node *incrExpr = n->children->data[2];
        if (incrExpr) {
            emitNode(incrExpr);
        }
        emitLoop(beforeTest);
        patchJump(forJump, -1, NULL);
        patchBreaks(forJump, currentIseq()->tail);
        popScope(COMPILE_SCOPE_BLOCK);
        loopStart = oldLoopStart;
        break;
    }
    case FOREACH_STMT: {
        pushScope(COMPILE_SCOPE_BLOCK);
        vec_byte_t v_slots;
        vec_init(&v_slots);
        int numVars = n->children->length - 2;
        current->localCount++; // the iterator value
        int i = 0;
        for (i = 0; i < numVars; i++) {
            Token varName = n->children->data[i]->tok;
            uint8_t varSlot = declareVariable(&varName);
            vec_push(&v_slots, varSlot);
        }
        // iterator expression
        emitNode(n->children->data[i]);
        i++;

        emitOp0(OP_ITER); // push iterator value to stack
        int beforeIterNext = currentIseq()->byteCount+2;
        emitOp0(OP_ITER_NEXT);
        Insn *iterDone = emitJump(OP_JUMP_IF_FALSE_PEEK); // TODO: op_jump_if_undef?
        uint8_t slotNum = 0; int slotIdx = 0;
        int setOp = numVars > 1 ? OP_UNPACK_SET_LOCAL : OP_SET_LOCAL;
        vec_foreach(&v_slots, slotNum, slotIdx) {
            if (setOp == OP_SET_LOCAL) {
                emitOp1(setOp, slotNum);
            } else {
                emitOp2(setOp, slotNum, (uint8_t)slotIdx);
            }
        }
        emitOp0(OP_POP); // pop the iterator value
        emitNode(n->children->data[i]); // foreach block
        emitLoop(beforeIterNext);
        popScope(COMPILE_SCOPE_BLOCK);
        patchJump(iterDone, -1, NULL);
        emitOp0(OP_POP); // pop last iterator value
        emitOp0(OP_POP); // pop the iterator
        vec_deinit(&v_slots);
        break;
    }
    case BREAK_STMT: {
        if (loopStart == -1) {
            error("'break' can only be used in loops ('while' or 'for' loops)");
        }
        Insn *in = emitJump(OP_JUMP);
        in->flags |= INSN_FL_BREAK;
        break; // I heard you like break statements, so I put a break in your break
    }
    case CONTINUE_STMT: {
        if (loopStart == -1) {
            error("'continue' can only be used in loops ('while' or 'for' loops)");
        }
        emitLoop(loopStart);
        break;
    }
    case PRINT_STMT: {
        emitChildren(n);
        emitOp0(OP_PRINT);
        return;
    }
    // single or multi-variable assignment, global or local
    case VAR_STMT: {
        int numVarsSet = 1;
        Node *lastNode = vec_last(n->children);
        bool uninitialized = nodeKind(lastNode) == VAR_STMT;
        if (uninitialized && n->children->length > 0) {
            numVarsSet += n->children->length;
        } else if (!uninitialized && n->children->length > 1) {
            numVarsSet += n->children->length - 1;
        }

        if (!uninitialized) {
            emitNode(lastNode); // expression node
            current->localCount++;
        } else {
            for (int i = 0; i < numVarsSet; i++) {
                emitNil();
            }
        }

        uint8_t slotIdx = 0;
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
                    emitOp1(OP_DEFINE_GLOBAL, (uint8_t)arg);
                } else {
                    ASSERT(0);
                    /*emitOp1(OP_UNPACK_DEFINE_GLOBAL, (uint8_t)arg, slotIdx);*/
                    /*slotIdx++;*/
                }
            } else {
                if (numVarsSet == 1 || uninitialized) {
                    emitOp1(OP_SET_LOCAL, (uint8_t)arg);
                } else {
                    emitOp2(OP_UNPACK_SET_LOCAL, (uint8_t)arg, slotIdx);
                    slotIdx++;
                }
            }
        }
        current->localCount--;

        return;
    }
    case VARIABLE_EXPR: {
        namedVariable(n->tok, VAR_GET);
        break;
    }
    case ASSIGN_EXPR: {
        emitNode(n->children->data[1]); // rval
        Node *varNode = vec_first(n->children);
        namedVariable(varNode->tok, VAR_SET);
        break;
    }
    case BLOCK_STMT: {
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
    case RETURN_STMT: {
        if (n->children->length > 0) {
            // always return instance in init function
            if (current->type == FUN_TYPE_INIT) {
                emitOp0(OP_GET_THIS);
            } else {
                emitChildren(n);
            }
            emitOp0(OP_RETURN);
            COMP_TRACE("Emitting explicit return (children)");
        } else {
            COMP_TRACE("Emitting explicit return (void)");
            emitReturn(current);
        }
        break;
    }
    case THIS_EXPR: {
        emitOp0(OP_GET_THIS);
        break;
    }
    case SUPER_EXPR: {
        Node *tokNode = vec_last(n->children);
        uint8_t methodNameArg = identifierConstant(&tokNode->tok);
        emitOp1(OP_GET_SUPER, methodNameArg);
        break;
    }
    case CALL_EXPR: {
        int nArgs = n->children->length-1;
        // arbitrary, but we don't want the VM op stack to blow by pushing a whole
        // bunch of arguments
        if (nArgs > 8) {
            error("too many arguments given to function (%d), maximum 8", nArgs);
            return;
        }
        Node *arg = NULL;
        int i = 0;
        Node *lhs = vec_first(n->children);
        int argc = n->children->length; // num regular arguments
        int numKwargs = 0;
        if (nodeKind(lhs) == PROP_ACCESS_EXPR) {
            emitChildren(lhs); // the instance
            uint8_t methodNameArg = identifierConstant(&lhs->tok);
            vec_foreach(n->children, arg, i) {
                if (i == 0) continue;
                if (arg->type.kind == KWARG_IN_CALL_STMT) {
                    argc--;
                    numKwargs++;
                }
                emitNode(arg);
            }
            CallInfo *callInfoData = calloc(sizeof(CallInfo), 1);
            ASSERT_MEM(callInfoData);
            callInfoData->nameTok = n->tok;
            callInfoData->argc = argc;
            callInfoData->numKwargs = numKwargs;
            i = 0; int idx = 0;
            vec_foreach(n->children, arg, i) {
                if (arg->type.kind == KWARG_IN_CALL_STMT) {
                    callInfoData->kwargNames[idx] = arg->tok;
                    idx++;
                }
            }
            ObjInternal *callInfoObj = newInternalObject(callInfoData, NULL, NULL);
            hideFromGC((Obj*)callInfoObj);
            uint8_t callInfoConstSlot = makeConstant(OBJ_VAL(callInfoObj), CONST_T_CALLINFO);
            emitOp3(OP_INVOKE, methodNameArg, nArgs, callInfoConstSlot);
        } else {
            emitNode(lhs); // the function itself
            i = 0;
            vec_foreach(n->children, arg, i) {
                if (i == 0) continue;
                if (arg->type.kind == KWARG_IN_CALL_STMT) {
                    argc--;
                    numKwargs++;
                }
                emitNode(arg);
            }
            CallInfo *callInfoData = calloc(sizeof(CallInfo), 1);
            ASSERT_MEM(callInfoData);
            callInfoData->nameTok = n->tok;
            callInfoData->argc = argc;
            callInfoData->numKwargs = numKwargs;
            i = 0; int idx = 0;
            vec_foreach(n->children, arg, i) {
                if (arg->type.kind == KWARG_IN_CALL_STMT) {
                    callInfoData->kwargNames[idx] = arg->tok;
                    idx++;
                }
            }
            ObjInternal *callInfoObj = newInternalObject(callInfoData, NULL, NULL);
            hideFromGC((Obj*)callInfoObj);
            uint8_t callInfoConstSlot = makeConstant(OBJ_VAL(callInfoObj), CONST_T_CALLINFO);
            emitOp2(OP_CALL, (uint8_t)nArgs, callInfoConstSlot);
        }
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
        int ifrom = iseq->byteCount;
        emitNode(n->children->data[0]); // try block
        Insn *jumpToEnd = emitJump(OP_JUMP);
        vec_push(&vjumps, jumpToEnd);
        int ito = iseq->byteCount;
        Node *catchStmt = NULL; int i = 0;
        if (n->children->length > 1) {
            vec_foreach(n->children, catchStmt, i) {
                if (i == 0) continue; // already emitted
                int itarget = iseq->byteCount;
                Token classTok = vec_first(catchStmt->children)->tok;
                ObjString *className = hiddenString(tokStr(&classTok), strlen(tokStr(&classTok)));
                double catchTblIdx = (double)iseqAddCatchRow(
                    iseq, ifrom, ito,
                    itarget, OBJ_VAL(className)
                );
                pushScope(COMPILE_SCOPE_BLOCK);
                // given variable expression to bind to (Ex: (catch Error err))
                if (catchStmt->children->length > 2) {
                    uint8_t getThrownArg = makeConstant(NUMBER_VAL(catchTblIdx), CONST_T_NUMLIT);
                    emitOp1(OP_GET_THROWN, getThrownArg);
                    Token varTok = catchStmt->children->data[1]->tok;
                    declareVariable(&varTok);
                    namedVariable(varTok, VAR_SET);
                }
                emitNode(vec_last(catchStmt->children)); // catch block
                ASSERT(iseq == currentIseq());
                /*if (catchStmt->children->length > 2) {*/
                    /*emitOp0(OP_POP); // pop the bound error variable*/
                /*}*/
                // don't emit a jump at the end of the final catch statement
                if (i < n->children->length-1) {
                    Insn *jumpStart = emitJump(OP_JUMP); // jump to end of try statement
                    vec_push(&vjumps, jumpStart);
                }
                popScope(COMPILE_SCOPE_BLOCK);
            }

            Insn *jump = NULL; int j = 0;
            vec_foreach(&vjumps, jump, j) {
                patchJump(jump, insnOffset(jump, currentIseq()->tail), currentIseq()->tail);
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

int compile_src(char *src, Chunk *chunk, CompileErr *err) {
    initScanner(&scanner, src);
    Compiler mainCompiler;
    top = &mainCompiler;
    initCompiler(&mainCompiler, 0, FUN_TYPE_TOP_LEVEL, NULL, chunk);
    initParser(&parser);
    Node *program = parse(&parser);
    if (CLOX_OPTION_T(parseOnly)) {
        *err = parser.hadError ? COMPILE_ERR_SYNTAX :
            COMPILE_ERR_NONE;
        return parser.hadError ? -1 : 0;
    } else if (parser.hadError) {
        *err = COMPILE_ERR_SYNTAX;
        return -1;
    }
    ASSERT(program);
    emitNode(program);
    ObjFunction *prog = endCompiler();
    *chunk = prog->chunk; // copy
    if (CLOX_OPTION_T(debugBytecode)) {
        printDisassembledChunk(stderr, chunk, "Bytecode:");
    }
    if (mainCompiler.hadError) {
        *err = COMPILE_ERR_SEMANTICS;
        return -1;
    } else {
        *err = COMPILE_ERR_NONE;
        return 0;
    }
}

int compile_file(char *fname, Chunk *chunk, CompileErr *err) {
    int fd = open(fname, O_RDONLY);
    if (fd == -1) {
        *err = COMPILE_ERR_ERRNO;
        return fd;
    }
    struct stat st;
    int res = fstat(fd, &st);
    if (res != 0) {
        *err = COMPILE_ERR_ERRNO;
        return res;
    }
    char *buf = calloc(st.st_size+1, 1);
    ASSERT_MEM(buf);
    if (buf == NULL) {
        *err = COMPILE_ERR_ERRNO;
        return -1;
    }
    memset(buf, 0, st.st_size+1);
    res = (int)read(fd, buf, st.st_size);
    if (res == -1) {
        *err = COMPILE_ERR_ERRNO;
        free(buf);
        return res;
    }
    res = compile_src(buf, chunk, err);
    free(buf);
    return res;
}

void grayCompilerRoots(void) {
    Compiler *compiler = current;
    while (compiler != NULL) {
        grayObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
