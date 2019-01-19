#include <stdio.h>
#include <stdbool.h>
#include "lox_jit.hpp"
#include "jit_compiler.hpp"
#include "nodes.h"
#include "scanner.h"
#include "parser.h"
#include "value.h"
#include "debug.h"
#include "options.h"
#include "memory.h"
#include "vm.h"

//#ifdef NDEBUG
//#define JIT_TRACE(lvl, ...) (void)0
//#else
#define JIT_TRACE(lvl, ...) jitTraceDebug(lvl, __VA_ARGS__)
//#endif

static llvm::LLVMContext theContext;
static llvm::IRBuilder<> theBuilder(theContext);
static std::unique_ptr<llvm::legacy::FunctionPassManager> theFPM;
static std::unique_ptr<llvm::Module> theModule;
static std::unique_ptr<llvm::orc::LoxJit> TheJIT;
static std::unique_ptr<llvm::Value> curFunction;
static bool jitInited = false;

static void jitTraceDebug(int lvl, const char *fmt, ...) {
    //if (!CLOX_OPTION_T(traceJit)) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[JIT]: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

void initJit() {
    JIT_TRACE(1, "initJit");
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    llvm::orc::LoxJit *jit = new llvm::orc::LoxJit();
    TheJIT = std::unique_ptr<llvm::orc::LoxJit>(jit);

    jitInited = true;
    JIT_TRACE(1, "/initJit");
}

void initJitModuleAndPassManager() {
    theModule = llvm::make_unique<llvm::Module>("clox_jit", theContext);
    theModule->setDataLayout(TheJIT->getTargetMachine().createDataLayout());

    // Create a new pass manager attached to it.
    theFPM = llvm::make_unique<llvm::legacy::FunctionPassManager>(theModule.get());
    // Do simple "peephole" optimizations and bit-twiddling optzns.
    theFPM->add(llvm::createInstructionCombiningPass());
    // Reassociate expressions.
    theFPM->add(llvm::createReassociatePass());
    // Eliminate Common SubExpressions.
    theFPM->add(llvm::createGVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    theFPM->add(llvm::createCFGSimplificationPass());
    theFPM->doInitialization();
}

llvm::orc::LoxJit::ModuleHandle jitAddModule() {
    ASSERT(theModule);
    auto m = TheJIT->addModule(std::move(theModule));
    initJitModuleAndPassManager();
    return m;
}

void jitRemoveModule(llvm::orc::LoxJit::ModuleHandle m) {
    TheJIT->removeModule(m);
}

Node *jitCreateAnonExpr(Node *n) {
    node_type_t funcType = {
        .type = NODE_STMT,
        .kind = FUNCTION_STMT,
    };
    Token nameTok;
    nameTok.type = TOKEN_IDENTIFIER;
    nameTok.start = "__anon_expr";
    nameTok.lexeme = NULL;
    nameTok.length = strlen("__anon_expr");
    nameTok.line = 1;
    nameTok.alloced = false;

    Node *funcNode = createNode(funcType, nameTok, NULL);

    vec_nodep_t *paramNodes = ALLOCATE(vec_nodep_t, 1);
    ASSERT_MEM(paramNodes);
    vec_init(paramNodes);
    nodeAddData(funcNode, paramNodes);
    nodeAddChild(funcNode, n);
    return funcNode;
}

void jitEvalAnonExpr() {
    // Search the JIT for the __anon_expr symbol.
    auto ExprSymbol = TheJIT->findSymbol("__anon_expr");
    ASSERT(ExprSymbol && "Function not found");

    // Get the symbol's address and cast it to the right type (takes no
    // arguments, returns a double) so we can call it as a native function.
    double (*FP)() = (double (*)())(intptr_t)ExprSymbol.getAddress();
    fprintf(stderr, "Evaluated to %f\n", FP());
}

static llvm::Value *jitNumber(Node *n) {
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
            int line = 1; // FIXME
            fprintf(stderr, "[Warning]: Decimal (base 10) number starting with '0' "
                    "found on line %d. If you wanted an octal number, the prefix is '0c' (ex: 0c644).\n", line);
        }
        d = strtod(numStr, NULL);
    }
    return llvm::ConstantFP::get(theContext, llvm::APFloat(d));
}

static llvm::Value *jitLiteral(Node *n) {
    ASSERT(n);
    if (n->tok.type == TOKEN_NUMBER) {
        return jitNumber(n);
    } else {
        UNREACHABLE_RETURN(NULL);
    }
}

static llvm::Value *jitBinop(Node *n) {
    ASSERT(n);
    const char *binOp = tokStr(&n->tok);
    llvm::Value *lhs = jitChild(n, 0);
    llvm::Value *rhs = jitChild(n, 1);
    if (strcmp(binOp, "+") == 0) {
        return theBuilder.CreateFAdd(lhs, rhs, "addtmp");
    } else if (strcmp(binOp, "-") == 0) {
        return theBuilder.CreateFSub(lhs, rhs, "subtmp");
    } else {
        ASSERT(0);
    }
}

static llvm::Value *jitBlock(Node *n) {
    ASSERT(n->children->length == 1);
    return jitChild(n, 0); // 1 child
}

static llvm::Value *jitExprStmt(Node *n) {
    ASSERT(n->children->length == 1);
    return jitChild(n, 0); // 1 child
}

static llvm::Value *jitStmtlist(Node *n) {
    llvm::Value *ret = nullptr;
    Node *ncur = nullptr;
    int nidx = 0;
    ASSERT(n->children->length > 0);
    vec_foreach(n->children, ncur, nidx) {
        ret = jitChild(n, nidx);
    }
    ASSERT(ret);
    return ret;
}

static llvm::Value *jitIfStmt(Node *n) {
    JIT_TRACE(1, "if condVal");
    llvm::Value *condVal = jitChild(n, 0);
    if (!condVal) {
        ASSERT(0);
        return nullptr;
    }
    // Convert condition to a bool by comparing equal to 0.0.
    condVal = theBuilder.CreateFCmpONE(
        condVal, llvm::ConstantFP::get(theContext, llvm::APFloat(0.0)), "ifcond"
    );
    llvm::Function *theFunction = theBuilder.GetInsertBlock()->getParent();
    ASSERT(theFunction);
    llvm::BasicBlock *thenBB =
        llvm::BasicBlock::Create(theContext, "then", theFunction);
    llvm::BasicBlock *elseBB =
        llvm::BasicBlock::Create(theContext, "else");
    llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(theContext, "ifcont");

    theBuilder.CreateCondBr(condVal, thenBB, elseBB);
    // Emit then value.
    theBuilder.SetInsertPoint(thenBB);

    JIT_TRACE(1, "if thenVal");
    llvm::Value *thenVal = jitChild(n, 1);
    if (!thenVal) {
        ASSERT(0);
        return nullptr;
    }
    theBuilder.CreateBr(mergeBB);

    // Codegen of 'Then' can change the current block, update ThenBB for the PHI.
    thenBB = theBuilder.GetInsertBlock();

    // emit else block
    theFunction->getBasicBlockList().push_back(elseBB);
    theBuilder.SetInsertPoint(elseBB);

    JIT_TRACE(1, "if elseVal");
    llvm::Value *elseVal = jitChild(n, 2);
    if (!elseVal) {
        ASSERT(0);
        return nullptr;
    }

    theBuilder.CreateBr(mergeBB);

    elseBB = theBuilder.GetInsertBlock();
    theFunction->getBasicBlockList().push_back(mergeBB);
    theBuilder.SetInsertPoint(mergeBB);

    llvm::PHINode *PN = theBuilder.CreatePHI(llvm::Type::getDoubleTy(theContext), 2, "iftmp");

    PN->addIncoming(thenVal, thenBB);
    PN->addIncoming(elseVal, elseBB);
    return PN;
}

// TODO: isolate per jitted function
static std::map<std::string, llvm::Value*> namedValues;

llvm::Value *jitFunction(Node *n) {
    namedValues.clear();
    std::string funcName(tokStr(&n->tok));
    vec_nodep_t *params = (vec_nodep_t*)nodeGetData(n);
    int argSize = params->length;
    // TODO: not all doubles!
    std::vector<llvm::Type*> doubles(argSize, llvm::Type::getDoubleTy(theContext));
    llvm::FunctionType *llvmFT =
        llvm::FunctionType::get(llvm::Type::getDoubleTy(theContext), doubles, false);

    llvm::Function *llvmFunc =
        llvm::Function::Create(llvmFT, llvm::Function::ExternalLinkage, funcName, theModule.get());

    // Set names for all arguments.
    int i = 0;
    for (auto &arg : llvmFunc->args()) {
        const char *cname = tokStr(&params->data[i]->tok);
        std::string argName(cname);
        arg.setName(argName);
        namedValues[argName] = &arg;
        i++;
    }
    // Create a new basic block to start insertion into.
    llvm::BasicBlock *bb = llvm::BasicBlock::Create(theContext, "entry", llvmFunc);
    theBuilder.SetInsertPoint(bb);
    llvm::Value *retVal = nullptr;
    ASSERT(n->children->length == 1);
    if ((retVal = jitChild(n, 0))) {
        // Finish off the function.
        theBuilder.CreateRet(retVal);
        // Validate the generated code, checking for consistency.
        llvm::verifyFunction(*llvmFunc);
        theFPM->run(*llvmFunc);
    } else {
        ASSERT(0);
    }

    curFunction = std::unique_ptr<llvm::Value>(llvmFunc);
    return llvmFunc;
}

llvm::Value *jitChild(Node *n, unsigned idx) {
    ASSERT(n->children->length >= (idx+1));
    return jitNode(n->children->data[idx]);
}

llvm::Value *jitNode(Node *n) {
    ASSERT(n);
    switch (nodeKind(n)) {
    case LITERAL_EXPR:
        JIT_TRACE(1, "emitting LITERAL");
        return jitLiteral(n);
    case FUNCTION_STMT:
        JIT_TRACE(1, "emitting FUNC");
        return jitFunction(n);
    case BINARY_EXPR:
        JIT_TRACE(1, "emitting BINOP");
        return jitBinop(n);
    case BLOCK_STMT:
        JIT_TRACE(1, "emitting BLOCK");
        return jitBlock(n);
    case STMTLIST_STMT:
        JIT_TRACE(1, "emitting SMTLIST (%d)", n->children->length);
        return jitStmtlist(n);
    case EXPR_STMT:
        JIT_TRACE(1, "emitting EXPR_STMT");
        return jitExprStmt(n);
    case IF_STMT:
        JIT_TRACE(1, "emitting IF_STMT");
        return jitIfStmt(n);
    default:
        fprintf(stderr, "Tried to jit node of kind %d: %s\n", nodeKind(n), nodeKindStr(nodeKind(n)));
        UNREACHABLE_RETURN(NULL);
    }
}

void jitEmitModuleIR(llvm::Module *m) {
    m->print(llvm::errs(), nullptr);
}

void jitEmitValueIR(llvm::Value *v) {
    v->print(llvm::errs());
}
