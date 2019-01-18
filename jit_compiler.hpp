#ifndef clox_jit_compiler_h
#define clox_jit_compiler_h

#ifndef __cplusplus
# error "cannot compile jit_compiler without C++ compiler"
#endif

#define CLOX_HAS_JIT 1

#include "common.h"
#include "object.h"
#include "nodes.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

llvm::Value *jitFunction(ObjFunction *func);
void initJit(void);
llvm::Value *jitNode(Node *n);
llvm::Value *jitChild(Node *n, unsigned idx);
void jitEmitModuleIR(llvm::Module *m);
void jitEmitValueIR(llvm::Value *v);

#endif
