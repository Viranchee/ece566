#ifndef DOMINANCE_H
#define DOMINANCE_H

//#include "llvm/Support/DataTypes.h"
//#include "llvm-c/Core.h"
#include "llvm/IR/Type.h"

#include "llvm/IR/BasicBlock.h"

using namespace llvm;

LLVM_C_EXTERN_C_BEGIN

bool LLVMDominates(Value *Fun, BasicBlock *a, BasicBlock *b);
bool LLVMPostDominates(Value *Fun, BasicBlock *a, BasicBlock *b);

BasicBlock *LLVMImmDom(BasicBlock *BB);
BasicBlock *LLVMImmPostDom(BasicBlock *BB);

BasicBlock *LLVMNearestCommonDominator(BasicBlock *A, BasicBlock *B);
unsigned LLVMGetLoopNestingDepth(BasicBlock *BB);

BasicBlock *LLVMFirstDomChild(BasicBlock *BB);
BasicBlock *LLVMNextDomChild(BasicBlock *BB, BasicBlock *Child);
bool LLVMIsReachableFromEntry(Value *Fun, BasicBlock *bb);

LLVM_C_EXTERN_C_END

#endif
