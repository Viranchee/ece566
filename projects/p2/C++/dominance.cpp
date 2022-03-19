/*
 * File: dominance.cpp
 *
 * Description:
 *   This provides a C interface to the dominance analysis in LLVM
 */

#include <stdio.h>
#include <stdlib.h>

/* LLVM Header Files */

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GlobalVariable.h"
//#include "llvm/PassManager.h"
#include "llvm/IR/Dominators.h"
//#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Type.h"

#include "dominance.h"

using namespace llvm;

Function *Current=NULL;
DominatorTreeBase<BasicBlock,false> *DT=NULL;
DominatorTreeBase<BasicBlock,true> *PDT=NULL;

LoopInfoBase<BasicBlock,Loop> *LI=NULL;

void UpdateDominators(Function *F)
{
  if (Current != F)
    {
      Current = F;

      if (DT==NULL)
	{
	  DT = new DominatorTreeBase<BasicBlock,false>();
	  PDT = new DominatorTreeBase<BasicBlock,true>();
	  if (LI==NULL)
	    LI = new LoopInfoBase<BasicBlock,Loop>();
	}
      
      DT->recalculate(*F);
      PDT->recalculate(*F);

      LI->analyze(*DT);
    }
}

// Test if a dom b
bool LLVMDominates(Value* Fun, BasicBlock* a, BasicBlock* b)
{
  UpdateDominators((Function*)Fun);
  return DT->dominates(a,b);
}

// Test if a pdom b
bool LLVMPostDominates(Value* Fun, BasicBlock* a, BasicBlock* b)
{
  UpdateDominators((Function*)Fun);
  return PDT->dominates(a,b);
}

bool LLVMIsReachableFromEntry(Value* Fun, BasicBlock* bb) {
  UpdateDominators((Function*)Fun);
  return DT->isReachableFromEntry(bb);
}


BasicBlock* LLVMImmDom(BasicBlock* BB)
{
  UpdateDominators(BB->getParent());

  if ( DT->getNode((BasicBlock*)BB) == NULL )
    return NULL;
  
  if ( DT->getNode((BasicBlock*)BB)->getIDom()==NULL )
    return NULL;

  return DT->getNode(BB)->getIDom()->getBlock();
}

BasicBlock* LLVMImmPostDom(BasicBlock* BB)
{
  UpdateDominators(BB->getParent());

  if (PDT->getNode(BB)->getIDom()==NULL)
    return NULL;

  return (BasicBlock*)PDT->getNode(BB)->getIDom()->getBlock();
}

BasicBlock* LLVMFirstDomChild(BasicBlock* BB)
{
  UpdateDominators(BB->getParent());
  DomTreeNodeBase<BasicBlock> *Node = DT->getNode(BB);

  if(Node==NULL)
    return NULL;

  DomTreeNodeBase<BasicBlock>::iterator it = Node->begin();
  if (it!=Node->end())
    return (*it)->getBlock();
  return NULL;
}

BasicBlock* LLVMNextDomChild(BasicBlock* BB, BasicBlock* Child)
{
  UpdateDominators(BB->getParent());
  DomTreeNodeBase<BasicBlock> *Node = DT->getNode(BB);
  DomTreeNodeBase<BasicBlock>::iterator it,end;

  bool next=false;
  for(it=Node->begin(),end=Node->end(); it!=end; it++)
    if (next)
      return (*it)->getBlock();
    else if (*it==DT->getNode(Child))
      next=true;

  return NULL;
}


BasicBlock* LLVMNearestCommonDominator(BasicBlock* A, BasicBlock* B)
{
  UpdateDominators(A->getParent());
  return DT->findNearestCommonDominator(A,B);
}

unsigned LLVMGetLoopNestingDepth(BasicBlock* BB)
{
  if (LI==NULL)
    UpdateDominators(BB->getParent());

  return LI->getLoopDepth(BB);
}


BasicBlock* LLVMDominanceFrontierLocal(BasicBlock* BB)
{
  return NULL;
}

BasicBlock* LLVMDominanceFrontierClosure(BasicBlock* BB)
{
  return NULL;
}

BasicBlock* LLVMPostDominanceFrontierLocal(BasicBlock* BB)
{
  return NULL;
}

BasicBlock* LLVMPostDominanceFrontierClosure(BasicBlock* BB)
{
  return NULL;
}
