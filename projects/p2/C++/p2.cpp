#include <algorithm>
#include <fstream>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "dominance.cpp"

#include "llvm-c/Core.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"

using namespace llvm;

static void CommonSubexpressionElimination(Module *);

static void summarize(Module *M);
static void print_csv_file(std::string outputfile);

static cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<input bitcode>"), cl::Required, cl::init("-"));

static cl::opt<std::string> OutputFilename(cl::Positional, cl::desc("<output bitcode>"), cl::Required, cl::init("out.bc"));

static cl::opt<bool> Mem2Reg("mem2reg", cl::desc("Perform memory to register promotion before CSE."), cl::init(false));

static cl::opt<bool> NoCSE("no-cse", cl::desc("Do not perform CSE Optimization."), cl::init(false));

static cl::opt<bool> Verbose("verbose", cl::desc("Verbose stats."), cl::init(false));

static cl::opt<bool> NoCheck("no", cl::desc("Do not check for valid IR."), cl::init(false));

int main(int argc, char **argv) {
  // Parse command line arguments
  cl::ParseCommandLineOptions(argc, argv, "llvm system compiler\n");

  // Handle creating output files and shutting down properly
  llvm_shutdown_obj Y; // Call llvm_shutdown() on exit.
  LLVMContext Context;

  // LLVM idiom for constructing output file.
  std::unique_ptr<ToolOutputFile> Out;
  std::string ErrorInfo;
  std::error_code EC;
  Out.reset(new ToolOutputFile(OutputFilename.c_str(), EC, sys::fs::OF_None));

  EnableStatistics();

  // Read in module
  SMDiagnostic Err;
  std::unique_ptr<Module> M;
  M = parseIRFile(InputFilename, Err, Context);

  // If errors, fail
  if (M.get() == 0) {
    Err.print(argv[0], errs());
    return 1;
  }

  // If requested, do some early optimizations
  if (Mem2Reg) {
    legacy::PassManager Passes;
    Passes.add(createPromoteMemoryToRegisterPass());
    Passes.run(*M.get());
  }

  if (!NoCSE) {
    CommonSubexpressionElimination(M.get());
  }

  // Collect statistics on Module
  summarize(M.get());
  print_csv_file(OutputFilename);

  if (Verbose)
    PrintStatistics(errs());

  // Verify integrity of Module, do this by default
  if (!NoCheck) {
    legacy::PassManager Passes;
    Passes.add(createVerifierPass());
    Passes.run(*M.get());
  }

  // Write final bitcode
  WriteBitcodeToFile(*M.get(), Out->os());
  Out->keep();

  return 0;
}

static llvm::Statistic nFunctions = {"", "Functions", "number of functions"};
static llvm::Statistic nInstructions = {"", "Instructions", "number of instructions"};
static llvm::Statistic nLoads = {"", "Loads", "number of loads"};
static llvm::Statistic nStores = {"", "Stores", "number of stores"};

static void summarize(Module *M) {
  for (auto i = M->begin(); i != M->end(); i++) {
    if (i->begin() != i->end()) {
      nFunctions++;
    }

    for (auto j = i->begin(); j != i->end(); j++) {
      for (auto k = j->begin(); k != j->end(); k++) {
        Instruction &I = *k;
        nInstructions++;
        if (isa<LoadInst>(&I)) {
          nLoads++;
        } else if (isa<StoreInst>(&I)) {
          nStores++;
        }
      }
    }
  }
}

static void print_csv_file(std::string outputfile) {
  std::ofstream stats(outputfile + ".stats");
  auto a = GetStatistics();
  for (auto p : a) {
    stats << p.first.str() << "," << p.second << std::endl;
  }
  stats.close();
}

static llvm::Statistic CSEDead = {"", "CSEDead", "CSE found dead instructions"};
static llvm::Statistic CSEElim = {"", "CSEElim", "CSE redundant instructions"};
static llvm::Statistic CSESimplify = {"", "CSESimplify", "CSE simplified instructions"};
static llvm::Statistic CSELdElim = {"", "CSELdElim", "CSE redundant loads"};
static llvm::Statistic CSEStore2Load = {"", "CSEStore2Load", "CSE forwarded store to load"};
static llvm::Statistic CSEStElim = {"", "CSEStElim", "CSE redundant stores"};

// Function Signatures
bool isDead(Instruction &I);
void basicCSEPass(BasicBlock::iterator &inputIterator);
void eliminateRedundantLoads(LoadInst *loadInst, BasicBlock::iterator &inputIterator);
void eliminateRedundantStores(StoreInst *storeInst, BasicBlock::iterator &originalIterator);

static void CommonSubexpressionElimination(Module *M) {

  // Iterate over all instructions in the module
  for (auto funcIter = M->begin(); funcIter != M->end(); funcIter++) {
    for (auto blockIter = funcIter->begin(); blockIter != funcIter->end(); blockIter++) {
      for (auto instIter = blockIter->begin(); instIter != blockIter->end();) {
        Instruction *I = &*instIter;
        auto tempIter = instIter;
        // Dead code elimination
        if (isDead(*I)) {
          instIter++;
          I->eraseFromParent();
          CSEDead++;
          continue;
        }

        // Simplify instructions
        if (auto simplified = SimplifyInstruction(I, M->getDataLayout())) {
          instIter++;
          I->replaceAllUsesWith(simplified);
          I->eraseFromParent();
          CSESimplify++;
          continue;
        }

        // Optimization 1: Common Subexpression Elimination
        auto copyIterator = instIter;
        basicCSEPass(copyIterator);

        // Optimization 2: Eliminate Redundant Loads
        if (I->getOpcode() == Instruction::Load) {
          auto copyIterator = instIter;
          LoadInst *loadInst = cast<LoadInst>(I);
          eliminateRedundantLoads(loadInst, copyIterator);
        }

        // Optimization 3: Eliminate Redundant Stores
        if (I->getOpcode() == Instruction::Store) {
          StoreInst *storeInst = cast<StoreInst>(I);
          eliminateRedundantStores(storeInst, instIter);
        }

        // TODO: Test functions in Dominance.cpp

        if (instIter == tempIter) {
          instIter++;
        }
      }
    }
  }
}

// Implementation

bool shouldCSEworkOnInstruction(Instruction *I) {
  // Early exit
  if (I->isTerminator()) {
    return false;
  }
  auto opcode = I->getOpcode();
  switch (opcode) {
  case Instruction::Load:
  case Instruction::Store:
  case Instruction::VAArg:
  case Instruction::Call:
  case Instruction::CallBr:
  case Instruction::Alloca:
  case Instruction::FCmp:
    return false;
  }
  return true;
}

bool isLiteralMatch(Instruction *i1, Instruction *i2) {
  // Defensive checks: Match Opcode, Type, #Operands, and operand order
  if (!((i1->getOpcode() == i2->getOpcode()) && (i1->getType() == i2->getType()) && (i1->getNumOperands() == i2->getNumOperands()))) {
    return false;
  }
  for (int i = 0; i < i1->getNumOperands(); i++) {
    if (!(i1->getOperand(i) == i2->getOperand(i))) {
      return false;
    }
  }
  return true;
}

bool isDead(Instruction &I) {
  // Check necessary requirements, otherwise return false
  if (I.use_begin() == I.use_end()) {
    int opcode = I.getOpcode();
    switch (opcode) {
    case Instruction::Add:
    case Instruction::FNeg:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::GetElementPtr:
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::PtrToInt:
    case Instruction::IntToPtr:
    case Instruction::BitCast:
    case Instruction::AddrSpaceCast:
    case Instruction::ICmp:
    case Instruction::FCmp:
    case Instruction::PHI:
    case Instruction::Select:
    case Instruction::ExtractElement:
    case Instruction::InsertElement:
    case Instruction::ShuffleVector:
    case Instruction::ExtractValue:
    case Instruction::InsertValue:
      return true; // dead, but this is not enough
    default:
      // any other opcode fails
      return false;
    }
  }
  return false;
}

void removeCommonInstructionsIn(BasicBlock::iterator iterator, BasicBlock *bb, Instruction *I) {
  for (auto instIter = iterator; instIter != bb->end();) {
    Instruction *nextInstruction = &*instIter;
    instIter++;
    if (I != nextInstruction && isLiteralMatch(I, nextInstruction)) {
      nextInstruction->replaceAllUsesWith(I);
      nextInstruction->eraseFromParent();
      CSEElim++;
    }
  }
}

// TODO: Change DOMTREE Implementation using dominance.h
DomTreeNodeBase<BasicBlock> *getDomTree(Instruction *I) {
  auto *bb = I->getParent();
  auto *F = bb->getParent();
  DominatorTreeBase<BasicBlock, false> *DT = new DominatorTreeBase<BasicBlock, false>();
  DT->recalculate(*F);                                 // F is Function*. Use one DominatorTreeBase and
                                                       // recalculate tree for each function you visit
  DomTreeNodeBase<BasicBlock> *Node = DT->getNode(bb); // get Node from some basic block*
  return Node;
}

void removeCommonInstInDominatedBlocks(Instruction *I) {

  auto *Node = getDomTree(I);
  DomTreeNodeBase<BasicBlock>::iterator it, end;
  for (it = Node->begin(), end = Node->end(); it != end; it++) {
    BasicBlock *bb_next = (*it)->getBlock(); // get each bb it immediately adominates
                                             // Iterate over all instructions in bb_next
    //  Print the dominated block
    // errs() << I->getParent()->getName() << " dom " << bb_next->getName() << "\tI: " << *I << "\n";

    removeCommonInstructionsIn(bb_next->begin(), bb_next, I);
  }
}

// Function which takes Instruction and returns a string
void basicCSEPass(BasicBlock::iterator &inputIterator) {
  auto *I = &*inputIterator;
  // Defensive checks, Early exit
  if (shouldCSEworkOnInstruction(I)) {
    // Remove common instructions in the same basic block
    removeCommonInstructionsIn(inputIterator, I->getParent(), I);

    // Remove common instructions in the same function, next block
    removeCommonInstInDominatedBlocks(I);
  }
}

void eliminateRedundantLoads(LoadInst *loadInst, BasicBlock::iterator &inputIterator) {
  auto *bb = inputIterator->getParent();
  inputIterator++;
  for (auto iterator = inputIterator; iterator != bb->end();) {
    Instruction *nextInst = &*iterator;
    iterator++;
    // Print nextInst
    if (nextInst->getOpcode() == Instruction::Load && !nextInst->isVolatile() && loadInst->getType() == nextInst->getType() &&
        loadInst->getOperand(0) == nextInst->getOperand(0)) {
      nextInst->replaceAllUsesWith(loadInst);
      nextInst->eraseFromParent();
      CSELdElim++;
    }
    if (nextInst->getOpcode() == Instruction::Store) {
      break;
    }
  }
}

/*
Eliminate redundant stores and loads followed by a load
TODO: Call instructions: You should treat call instructions as stores to an unknown and possibly same address as S or L
@params *storeInst: StoreInst to be worked on
@params &originalIterator: Iterator to the store instruction
@returns void: Just runs function
 */
void eliminateRedundantStores(StoreInst *storeInst, BasicBlock::iterator &originalIterator) {
  bool _break = false;
  auto storedValue = storeInst->getValueOperand();
  auto storedAddress = storeInst->getPointerOperand();

  auto copyIterator = originalIterator;
  BasicBlock *bb = copyIterator->getParent();
  copyIterator++;
  while (copyIterator != bb->end()) {
    // Get next instruction
    Instruction *nextInst = &*copyIterator;

    auto isLoad = nextInst->getOpcode() == Instruction::Load;
    auto loadIsNotVolatile = !nextInst->isVolatile();

    if (isLoad && loadIsNotVolatile && nextInst->getOperand(0) == storedAddress && nextInst->getType() == storedValue->getType()) {
      copyIterator++;
      nextInst->replaceAllUsesWith(storedValue);
      nextInst->eraseFromParent();
      CSEStore2Load++;
      continue;
    }
    auto isStore = nextInst->getOpcode() == Instruction::Store;
    auto initialStoreNotVolatile = !storeInst->isVolatile();
    if (isStore && initialStoreNotVolatile && storedAddress == nextInst->getOperand(1) &&
        storedValue->getType() == nextInst->getOperand(0)->getType()) {
      copyIterator++;
      originalIterator++;
      storeInst->eraseFromParent();
      CSEStElim++;
      break;
    }
    if (isLoad || isStore) {
      break;
    }
    copyIterator++;
  }
  return;
}
