#include <stdio.h>
#include <stdbool.h>
#include "jit_compiler.hpp"
#include "nodes.h"
#include "scanner.h"
#include "parser.h"
#include "value.h"
#include "debug.h"
#include "options.h"
#include "memory.h"
#include "vm.h"

#ifdef NDEBUG
#define JIT_TRACE(...) (void)0
#else
#define JIT_TRACE(...) jitTraceDebug(__VA_ARGS__)
#endif

static llvm::LLVMContext theContext;
static llvm::IRBuilder<> theBuilder(theContext);
static std::unique_ptr<llvm::Module> theModule;
static bool jitInited = false;

static void jitTraceDebug(const char *fmt, ...) {
    if (!CLOX_OPTION_T(traceCompiler)) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[JIT]: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

void initJit() {
    theModule = llvm::make_unique<llvm::Module>("clox_jit", theContext);
    jitInited = true;
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
    if (n->tok.type == TOKEN_NUMBER) {
        return jitNumber(n);
    } else {
        UNREACHABLE_RETURN(NULL);
    }
}

static llvm::Value *jitBinop(Node *n) {
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
    return jitChild(n, 0);
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
    if ((retVal = jitChild(n, 0))) {
        // Finish off the function.
        theBuilder.CreateRet(retVal);
        // Validate the generated code, checking for consistency.
        llvm::verifyFunction(*llvmFunc);

    } else {
        ASSERT(0);
    }

    return llvmFunc;
}

llvm::Value *jitChild(Node *n, unsigned idx) {
    ASSERT(n->children->length >= (idx+1));
    return jitNode(n->children->data[idx]);
}

llvm::Value *jitNode(Node *n) {
    switch (nodeKind(n)) {
    case LITERAL_EXPR:
        return jitLiteral(n);
    case FUNCTION_STMT:
        return jitFunction(n);
    case BINARY_EXPR:
        return jitBinop(n);
    case BLOCK_STMT:
        return jitBlock(n);
    default:
            UNREACHABLE_RETURN(NULL);
    }
}

void jitEmitModuleIR(llvm::Module *m) {
    m->print(llvm::errs(), nullptr);
}

void jitEmitValueIR(llvm::Value *v) {
    v->print(llvm::errs());
}
