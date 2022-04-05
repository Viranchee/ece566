#include <algorithm>
#include <fstream>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode>"),
                                          cl::Required,
                                          cl::init("-"));

static cl::opt<std::string> OutputFilename(cl::Positional,
                                           cl::desc("<output bitcode>"),
                                           cl::Required,
                                           cl::init("out.bc"));

static cl::opt<bool>
    Mem2Reg("mem2reg",
            cl::desc("Perform memory to register promotion before CSE."),
            cl::init(false));

static cl::opt<bool> NoCSE("no-cse",
                           cl::desc("Do not perform CSE Optimization."),
                           cl::init(false));

static cl::opt<bool>
    Verbose("verbose", cl::desc("Verbose stats."), cl::init(false));

static cl::opt<bool>
    NoCheck("no", cl::desc("Do not check for valid IR."), cl::init(false));

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
static llvm::Statistic nInstructions = {"",
                                        "Instructions",
                                        "number of instructions"};
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
static llvm::Statistic CSESimplify = {"",
                                      "CSESimplify",
                                      "CSE simplified instructions"};
static llvm::Statistic CSELdElim = {"", "CSELdElim", "CSE redundant loads"};
static llvm::Statistic CSEStore2Load = {"",
                                        "CSEStore2Load",
                                        "CSE forwarded store to load"};
static llvm::Statistic CSEStElim = {"", "CSEStElim", "CSE redundant stores"};

// Function Signatures
bool isDead(Instruction &I);
void basicCSEPass(BasicBlock::iterator &inputIterator);
void eliminateRedundantLoads(LoadInst *loadInst,
                             BasicBlock::iterator &inputIterator);
void eliminateRedundantStoreCall(Instruction *storeCall,
                                 BasicBlock::iterator &originalIterator);
void printStats() {
  errs() << "STATS:\n";
  errs() << "CSE Dead:\t" << CSEDead << "\n";
  errs() << "CSE Elim:\t" << CSEElim << "\n";
  errs() << "CSE Simplify:\t" << CSESimplify << "\n";
  errs() << "CSE LdElim:\t" << CSELdElim << "\n";
  errs() << "CSE Store2Load:\t" << CSEStore2Load << "\n";
  errs() << "CSE StElim:\t" << CSEStElim << "\n";
  errs() << "CSE Total:\t"
         << CSEDead + CSEElim + CSESimplify + CSELdElim + CSEStore2Load +
                CSEStElim
         << "\n";
}
static void CommonSubexpressionElimination(Module *M) {

  // Iterate over all instructions in the module
  for (auto funcIter = M->begin(); funcIter != M->end(); funcIter++) {
    for (auto blockIter = funcIter->begin(); blockIter != funcIter->end();
         blockIter++) {
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
          eliminateRedundantStoreCall(I, instIter);
        }

        if (instIter == tempIter) {
          instIter++;
        }
      }
    }
  }

  // TODO Print out statistics
  printStats();
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
  default:
    break;
  }
  return true;
}

bool sameOpcode(Instruction *I1, Instruction *I2) {
  return I1->getOpcode() == I2->getOpcode();
}
bool sameType(Instruction *I1, Instruction *I2) {
  return I1->getType() == I2->getType();
}
bool sameOperands(Instruction *I1, Instruction *I2) {

  auto numOperands = I1->getNumOperands();
  if (numOperands != I2->getNumOperands()) {
    return false;
  }
  for (unsigned i = 0; i < numOperands; i++) {
    if (I1->getOperand(i) != I2->getOperand(i)) {
      return false;
    }
  }
  return true;
}

bool notVolatile(Instruction *I) { return !I->isVolatile(); }

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

void removeCommonInstructionsIn(BasicBlock::iterator iterator,
                                BasicBlock *bb,
                                Instruction *I) {
  for (auto instIter = iterator; instIter != bb->end();) {
    Instruction *nextInstruction = &*instIter;
    instIter++;
    if (I != nextInstruction && I->isIdenticalTo(nextInstruction)) {
      nextInstruction->replaceAllUsesWith(I);
      nextInstruction->eraseFromParent();
      CSEElim++;
    }
  }
}

DomTreeNodeBase<BasicBlock> *getDomTree(Instruction *I) {
  auto *bb = I->getParent();
  auto *F = bb->getParent();
  DominatorTreeBase<BasicBlock, false> *DT =
      new DominatorTreeBase<BasicBlock, false>();
  DT->recalculate(*F); // F is Function*. Use one DominatorTreeBase and
                       // recalculate tree for each function you visit
  DomTreeNodeBase<BasicBlock> *Node =
      DT->getNode(bb); // get Node from some basic block*
  return Node;
}

void removeCommonInstInDominatedBlocks(Instruction *I) {
  auto *Node = getDomTree(I);
  DomTreeNodeBase<BasicBlock>::iterator it, end;
  for (it = Node->begin(), end = Node->end(); it != end; it++) {
    BasicBlock *bb_next =
        (*it)->getBlock(); // get each bb it immediately adominates
                           // Iterate over all instructions in bb_next
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

bool isCall(Instruction *I) {
  return I->getOpcode() == Instruction::Call;
}

void eliminateRedundantLoads(LoadInst *loadInst,
                             BasicBlock::iterator &inputIterator) {
  auto *bb = inputIterator->getParent();
  inputIterator++;
  for (auto iterator = inputIterator; iterator != bb->end();) {
    Instruction *nextInst = &*iterator;
    iterator++;
    // Print nextInst
    if (sameOpcode(loadInst, nextInst) && notVolatile(nextInst) &&
        sameType(loadInst, nextInst) && sameOperands(loadInst, nextInst)) {
      nextInst->replaceAllUsesWith(loadInst);
      nextInst->eraseFromParent();
      CSELdElim++;
    }
    if (nextInst->getOpcode() == Instruction::Store) {
      break;
    }
  }
}

bool sameAddress(StoreInst *i1, StoreInst *i2) {
  return i1->getPointerOperand() == i2->getPointerOperand();
}
bool sameValue(StoreInst *i1, StoreInst *i2) {
  return i1->getValueOperand() == i2->getValueOperand();
}
bool sameDataType(StoreInst *i1, StoreInst *i2) {
  return i1->getValueOperand()->getType() == i2->getValueOperand()->getType();
}

bool sameAddress(StoreInst *i1, LoadInst *i2) {
  return i1->getPointerOperand() == i2->getPointerOperand();
}
bool sameDataType(StoreInst *i1, LoadInst *i2) {
  return i1->getValueOperand()->getType() == i2->getType();
}

/*
Eliminate redundant stores and loads followed by a load
TODO: Call instructions: You should treat call instructions as stores to an
unknown and possibly same address as S or L
@params *storeInst: StoreInst to be worked on
@params &originalIterator: Iterator to the store instruction
@returns void: Just runs function
 */
void eliminateRedundantStoreCall(Instruction *storeCall,
                                 BasicBlock::iterator &originalIterator) {
  auto storeInst = dyn_cast<StoreInst>(storeCall);

  auto copyIterator = originalIterator;
  BasicBlock *bb = copyIterator->getParent();
  copyIterator++;
  while (copyIterator != bb->end()) {
    // Get next instruction
    Instruction *nextInst = &*copyIterator;
    auto nextLoad = dyn_cast<LoadInst>(nextInst);
    auto nextStore = dyn_cast<StoreInst>(nextInst);

    if (storeInst && nextLoad && notVolatile(nextInst) &&
        sameAddress(storeInst, nextLoad) && sameDataType(storeInst, nextLoad)) {
      copyIterator++;
      nextInst->replaceAllUsesWith(storeInst->getValueOperand());
      nextInst->eraseFromParent();
      CSEStore2Load++;
      continue;
    } else if (storeInst && nextStore && notVolatile(storeInst) &&
               sameAddress(storeInst, nextStore) &&
               sameDataType(storeInst, nextStore)) {
      copyIterator++;
      originalIterator++;
      storeInst->eraseFromParent();
      CSEStElim++;
      break;
    } else if (nextLoad || nextStore || isCall(nextInst)) {
      break;
    }
    copyIterator++;
  }
  return;
}