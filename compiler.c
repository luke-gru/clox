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
#define COMP_TRACE(...) (void(0))
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
  bool emittedReturn; // has emitted at least 1 return for this function so far
  vec_int_t emittedReturnDepths;

  Iseq iseq; // Generated instructions for the function
  Table constTbl;
} Compiler;


typedef struct ClassCompiler {
  struct ClassCompiler *enclosing;
  Token name;
  bool hasSuperclass;
} ClassCompiler;

typedef enum {
    COMPILE_SCOPE_BLOCK = 1,
    COMPILE_SCOPE_FUNCTION,
    COMPILE_SCOPE_CLASS,
    COMPILE_SCOPE_MODULE, // TODO
} CompileScopeType;

static const char *compileScopeName(CompileScopeType stype) {
    switch (stype) {
    case COMPILE_SCOPE_BLOCK: return "SCOPE_BLOCK";
    case COMPILE_SCOPE_FUNCTION: return "SCOPE_FUNCTION";
    case COMPILE_SCOPE_CLASS: return "SCOPE_CLASS";
    case COMPILE_SCOPE_MODULE: return "SCOPE_MODULE";
    default: {
        UNREACHABLE("invalid scope type: %d", stype);
    }
    }
}

static Compiler *current = NULL;
static Compiler *top = NULL;
static ClassCompiler *currentClass = NULL;
static Token *curTok = NULL;
CompilerOpts compilerOpts; // [external]

typedef enum {
    VAR_GET = 1,
    VAR_SET,
} VarOp;

typedef enum {
    CONST_T_NUMLIT = 1,
    CONST_T_STRLIT,
    CONST_T_CODE,
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
    COMP_TRACE("pushScope: %s", compileScopeName(stype));
}

static void namedVariable(Token name, VarOp getSet);

static bool emittedReturnInScope(Compiler *comp) {
    if (comp->emittedReturn) {
        int idx = 0;
        // NOTE: add 1 here because the explicit return would be in the block scope
        // of the function, so it added a scopeDepth
        vec_find(&comp->emittedReturnDepths, comp->scopeDepth+1, idx);
        if (idx != -1) {
            return true;
        }
    }
    return false;
}

// returns nil, this is in case OP_RETURN wasn't emitted from an explicit
// `return` statement in a function.
static void emitReturn(Compiler *compiler) {
    ASSERT(compiler->type != FUN_TYPE_TOP_LEVEL);
    if (emittedReturnInScope(compiler)) {
        COMP_TRACE("Skipping emitting return");
        return;
    }
    COMP_TRACE("Emitting return");
    if (compiler->type == FUN_TYPE_INIT) {
        namedVariable(syntheticToken("this"), VAR_GET);
        emitOp0(OP_RETURN);
    } else {
        emitOp0(OP_NIL);
        emitOp0(OP_RETURN);
    }
    compiler->emittedReturn = true;
    vec_push(&compiler->emittedReturnDepths, compiler->scopeDepth);
}

static void emitCloseUpvalue(void) {
    if (emittedReturnInScope(current)) {
        COMP_TRACE("Skipping emitting close upvalue (returned)");
        return;
    }
    COMP_TRACE("Emitting close upvalue");
    emitOp0(OP_CLOSE_UPVALUE);
}

static void popScope(CompileScopeType stype) {
    COMP_TRACE("popScope: %s", compileScopeName(stype));
    while (current->localCount > 0 &&
            current->locals[current->localCount - 1].depth >= current->scopeDepth) {
        if (stype != COMPILE_SCOPE_CLASS) {
            if (current->locals[current->localCount - 1].isUpvalue) {
                COMP_TRACE("popScope closing upvalue");
                emitCloseUpvalue();
            } else {
                COMP_TRACE("popScope emitting OP_POP");
                emitOp0(OP_POP); // don't pop the non-pushed implicit 'super' in class scope
            }
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

  // If we got here, it's a new upvalue.
  if (compiler->function->upvalueCount == 256) {
    error("Too many closure variables in function.");
    return 0;
  }

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

static void writeChunkByte(Chunk *chunk, uint8_t byte, int lineno) {
    writeChunk(chunk, byte, lineno);
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
        case OP_ADD: {
            return NUMBER_VAL(aNum + bNum);
        }
        case OP_SUBTRACT:
            return NUMBER_VAL(aNum - bNum);
        case OP_MULTIPLY:
            return NUMBER_VAL(aNum * bNum);
        case OP_DIVIDE:
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
    Insn *cur = iseq->insns;
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
        writeChunkByte(chunk, in->code, in->lineno);
        for (int i = 0; i < in->numOperands; i++) {
            writeChunkByte(chunk, in->operands[i], in->lineno);
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
    vec_deinit(&current->emittedReturnDepths);
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
    ASSERT(vm.inited);
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)),
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
    vec_foreach(n->children, stmt, i) {
        emitNode(stmt);
    }
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
        jumpoffset = insnOffset(toPatch, jumpTo)+1;
    }
    toPatch->operands[0] = jumpoffset;
    toPatch->jumpTo = jumpTo;
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
            int offset = insnOffset(cur, end);
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
    compiler->localCount = 0;
    compiler->scopeDepth = scopeDepth;
    compiler->function = newFunction(chunk);
    initIseq(&compiler->iseq);
    hideFromGC((Obj*)compiler->function); // TODO: figure out way to unhide these functions on freeVM()
    compiler->type = ftype;
    compiler->hadError = false;
    compiler->emittedReturn = false;
    vec_init(&compiler->emittedReturnDepths);
    initTable(&compiler->constTbl);

    current = compiler;

    switch (ftype) {
    case FUN_TYPE_NAMED:
        current->function->name = copyString(
            tokStr(fTok), strlen(tokStr(fTok))
        );
        break;
    case FUN_TYPE_INIT:
    case FUN_TYPE_METHOD: {
        ASSERT(currentClass);
        char *className = tokStr(&currentClass->name);
        char *funcName = tokStr(fTok);
        size_t methodNameBuflen = strlen(className)+1+strlen(funcName)+1; // +1 for '.' in between
        char *methodNameBuf = calloc(methodNameBuflen, 1);
        ASSERT_MEM(methodNameBuf);
        strcpy(methodNameBuf, className);
        strncat(methodNameBuf, ".", 1);
        strcat(methodNameBuf, funcName);
        ObjString *methodName = copyString(methodNameBuf, strlen(methodNameBuf));
        current->function->name = methodName;
        free(methodNameBuf);
        break;
    }
    case FUN_TYPE_ANON:
    case FUN_TYPE_TOP_LEVEL:
        current->function->name = NULL;
        break;
    default:
        error("invalid function type %d", ftype);
    }

    // The first slot is always implicitly declared.
    Local* local = &current->locals[current->localCount++];
    local->depth = current->scopeDepth;
    local->isUpvalue = false;
    if (ftype == FUN_TYPE_METHOD || ftype == FUN_TYPE_INIT) {
        // In a method, it holds the receiver, "this".
        local->name.start = "this";
        local->name.length = 4;
    } else {
        // In a function, it holds the function, but cannot be referenced, so has
        // no name.
        local->name.start = "";
        local->name.length = 0;
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

/*static bool assignExprValueUnused(Node *assignNode) {*/
    /*Node *parent = assignNode->parent;*/
    /*NodeType nType = parent->type.type;*/
    /*int nKind = parent->type.kind;*/
    /*while (parent && nKind == GROUPING_EXPR) {*/
        /*parent = parent->parent;*/
        /*nType = parent->type.type;*/
        /*nKind = parent->type.kind;*/
    /*}*/
    /*if (nType == NODE_STMT) {*/
        /*return true;*/
    /*} else {*/
        /*return false;*/
    /*}*/
/*}*/

static void emitClass(Node *n) {
    uint8_t nameConstant = identifierConstant(&n->tok);
    ClassCompiler cComp;
    memset(&cComp, 0, sizeof(cComp));
    cComp.name = n->tok;
    Token *superClassTok = (Token*)nodeGetData(n);
    cComp.hasSuperclass = superClassTok != NULL;
    cComp.enclosing = currentClass;
    currentClass = &cComp;

    if (cComp.hasSuperclass) {
        pushScope(COMPILE_SCOPE_CLASS);
        // get the superclass
        namedVariable(*superClassTok, VAR_GET);
        // Store the superclass in a local variable named "super".
        addLocal(syntheticToken("super"));

        emitOp1(OP_SUBCLASS, nameConstant); // VM pops the superclass and gets the class name
    } else {
        emitOp1(OP_CLASS, nameConstant); // VM gets the class name
    }

    emitChildren(n); // block node with methods

    if (cComp.hasSuperclass) {
        popScope(COMPILE_SCOPE_CLASS);
    }

    if (current->scopeDepth == 0) {
        defineVariable(nameConstant, false);
    } else {
        uint8_t defineArg = declareVariable(&n->tok);
        defineVariable(defineArg, true);
    }
    currentClass = cComp.enclosing;
}

// emit function or method
static void emitFunction(Node *n, FunctionType ftype) {
    Compiler fCompiler;
    initCompiler(&fCompiler, current->scopeDepth,
        ftype, &n->tok, NULL);
    pushScope(COMPILE_SCOPE_FUNCTION); // this scope holds the local variable parameters

    ObjFunction *func = fCompiler.function;

    vec_nodep_t *params = (vec_nodep_t*)nodeGetData(n);
    ASSERT(params);
    Node *param = NULL; int i = 0;
    vec_foreach(params, param, i) {
        uint8_t localSlot = declareVariable(&param->tok);
        defineVariable(localSlot, true);
        func->arity++;
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

    if (ftype != FUN_TYPE_ANON) {
        if (currentClass == NULL) {
            uint8_t defineArg;
            if (current->scopeDepth > 0) {
                defineArg = declareVariable(&n->tok);
            } else {
                defineArg = identifierConstant(&n->tok);
            }
            defineVariable(defineArg, true); // define function as global or local var
        // TODO: allow regular function definitions in classes, along with methods
        } else {
            emitOp1(OP_METHOD, identifierConstant(&n->tok));
        }
    }
}

static void emitNode(Node *n) {
    if (current->hadError) return;
    curTok = &n->tok;
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
        } else {
            error("invalid binary expr node (token: %s)", tokStr(&n->tok));
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
            error("invalid logical expression node (token: %s)", tokStr(&n->tok));
            ASSERT(0);
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
            error("invalid unary expr node (token: %s)", tokStr(&n->tok));
        }
        return;
    }
    case LITERAL_EXPR: {
        if (n->tok.type == TOKEN_NUMBER) {
            // TODO: handle error condition
            double d = strtod(tokStr(&n->tok), NULL);
            emitConstant(NUMBER_VAL(d), CONST_T_NUMLIT);
        } else if (n->tok.type == TOKEN_STRING) {
            Token *name = &n->tok;
            emitConstant(OBJ_VAL(copyString(name->start+1, name->length-2)), CONST_T_STRLIT);
        } else if (n->tok.type == TOKEN_TRUE) {
            emitOp0(OP_TRUE);
        } else if (n->tok.type == TOKEN_FALSE) {
            emitOp0(OP_FALSE);
        } else if (n->tok.type == TOKEN_NIL) {
            emitOp0(OP_NIL);
        } else {
            error("invalid literal expr node (token: %s)", tokStr(&n->tok));
        }
        return;
    }
    case ARRAY_EXPR: {
        emitChildren(n);
        emitConstant(NUMBER_VAL(n->children->length), CONST_T_NUMLIT);
        emitOp0(OP_CREATE_ARRAY);
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
            patchJump(ifJumpStart, -1, NULL);
            emitNode(elseNode);
        }
        break;
    }
    case WHILE_STMT: {
        Insn *loopLabel = currentIseq()->tail;
        int loopStart = currentIseq()->byteCount + 2;
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
        break;
    }
    case FOR_STMT: {
        pushScope(COMPILE_SCOPE_BLOCK);
        Node *init = vec_first(n->children);
        if (init) {
            emitNode(init);
        }
        Node *test = n->children->data[1];
        int beforeTest = currentIseq()->byteCount+2;
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
        break;
    }
    case BREAK_STMT: {
        Insn *in = emitJump(OP_JUMP);
        in->flags |= INSN_FL_BREAK;
        break; // I heard you like break statements, so I put a break in your break
    }
    case PRINT_STMT: {
        emitChildren(n);
        emitOp0(OP_PRINT);
        return;
    }
    case VAR_STMT: {
        int arg = declareVariable(&n->tok);
        if (arg == -1) return; // error already printed
        if (n->children->length > 0) {
            emitChildren(n);
        } else {
            emitNil();
        }
        if (current->scopeDepth == 0) {
            emitOp1(OP_DEFINE_GLOBAL, (uint8_t)arg);
        } else {
            emitOp1(OP_SET_LOCAL, (uint8_t)arg);
        }
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
        if (currentClass == NULL) {
            error("Methods can only be declared in classes. Maybe forgot keyword 'fun'?");
        } else {
            FunctionType ftype = FUN_TYPE_METHOD;
            if (strcmp(tokStr(&n->tok), "init") == 0) {
                ftype = FUN_TYPE_INIT;
            }
            emitFunction(n, ftype);
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
        if (current->emittedReturn) {
            int idx = 0;
            vec_find(&current->emittedReturnDepths, current->scopeDepth, idx);
            if (idx != -1) {
                COMP_TRACE("Skipping emitting explicit return");
                break;
            }
        }

        if (n->children->length > 0) {
            if (current->type == FUN_TYPE_INIT) {
                namedVariable(syntheticToken("this"), VAR_GET);
            } else {
                emitChildren(n);
            }
            emitOp0(OP_RETURN);
            COMP_TRACE("Emitting explicit return (children)");
            current->emittedReturn = true;
            vec_push(&current->emittedReturnDepths, current->scopeDepth);
        } else {
            COMP_TRACE("Emitting explicit return (void)");
            emitReturn(current);
        }
        break;
    }
    case THIS_EXPR: {
        namedVariable(syntheticToken("this"), VAR_GET);
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
        if (nodeKind(lhs) == PROP_ACCESS_EXPR) {
            emitChildren(lhs); // the instance
            uint8_t methodNameArg = identifierConstant(&lhs->tok);
            vec_foreach(n->children, arg, i) {
                if (i == 0) continue;
                emitNode(arg);
            }
            emitOp2(OP_INVOKE, methodNameArg, (uint8_t)nArgs);
        } else {
            emitNode(lhs);
            vec_foreach(n->children, arg, i) {
                if (i == 0) continue;
                emitNode(arg);
            }
            emitOp1(OP_CALL, (uint8_t)nArgs);
        }
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
                ObjString *className = copyString(tokStr(&classTok), strlen(tokStr(&classTok)));
                double catchTblIdx = (double)iseqAddCatchRow(
                    iseq, ifrom, ito,
                    itarget, OBJ_VAL(className)
                );
                pushScope(COMPILE_SCOPE_BLOCK);
                // given variable expression to bind to (Ex: (catch Error `err`))
                if (catchStmt->children->length > 2) {
                    uint8_t getThrownArg = makeConstant(NUMBER_VAL(catchTblIdx), CONST_T_NUMLIT);
                    emitOp1(OP_GET_THROWN, getThrownArg);
                    Token varTok = catchStmt->children->data[1]->tok;
                    declareVariable(&varTok);
                    namedVariable(varTok, VAR_SET);
                }
                emitNode(vec_last(catchStmt->children)); // catch block
                ASSERT(iseq == currentIseq());
                Insn *jumpStart = emitJump(OP_JUMP); // jump to end of try statement
                vec_push(&vjumps, jumpStart);
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
        error("invalid (unknown) node. kind (%d) not implemented (tok=%s)",
              nodeKind(n), tokStr(&n->tok)
        );
    }
}

int compile_src(char *src, Chunk *chunk, CompileErr *err) {
    initScanner(src);
    Compiler mainCompiler;
    top = &mainCompiler;
    initCompiler(&mainCompiler, 0, FUN_TYPE_TOP_LEVEL, NULL, chunk);
    Node *program = parse();
    if (CLOX_OPTION_T(parseOnly)) {
        *err = parser.hadError ? COMPILE_ERR_SYNTAX :
            COMPILE_ERR_NONE;
        return parser.hadError ? -1 : 0;
    } else if (parser.hadError) {
        *err = COMPILE_ERR_SYNTAX;
        return -1;
    }
    emitNode(program);
    ObjFunction *prog = endCompiler();
    *chunk = prog->chunk; // copy
    if (CLOX_OPTION_T(debugBytecode)) {
        printDisassembledChunk(chunk, "Bytecode:");
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
        free(buf);
        *err = COMPILE_ERR_ERRNO;
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
