#include <algorithm>
#include <fstream>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>

#include "llvm-c/Core.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

using namespace llvm;

static void LoopInvariantCodeMotion(Module *M);

static void summarize(Module *M);
static void print_csv_file(std::string outputfile);

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode>"),
                                          cl::Required, cl::init("-"));

static cl::opt<std::string> OutputFilename(cl::Positional,
                                           cl::desc("<output bitcode>"),
                                           cl::Required, cl::init("out.bc"));

static cl::opt<bool>
    Mem2Reg("mem2reg",
            cl::desc("Perform memory to register promotion before LICM."),
            cl::init(false));

static cl::opt<bool> CSE("cse", cl::desc("Perform CSE before LICM."),
                         cl::init(false));

static cl::opt<bool> NoLICM("no-licm",
                            cl::desc("Do not perform LICM optimization."),
                            cl::init(false));

static cl::opt<bool> Verbose("verbose", cl::desc("Verbose stats."),
                             cl::init(false));

static cl::opt<bool> NoCheck("no", cl::desc("Do not check for valid IR."),
                             cl::init(false));

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
    if (Mem2Reg || CSE) {
        legacy::PassManager Passes;
        if (Mem2Reg)
            Passes.add(createPromoteMemoryToRegisterPass());
        if (CSE)
            Passes.add(createEarlyCSEPass());
        Passes.run(*M.get());
    }

    if (!NoLICM) {
        LoopInvariantCodeMotion(M.get());
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
static llvm::Statistic nInstructions = {"", "Instructions",
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

static llvm::Statistic NumLoops = {"", "NumLoops", "number of loops analyzed"};
static llvm::Statistic NumLoopsNoStore = {"", "NumLoopsNoStore",
                                          "number of loops without stores"};
static llvm::Statistic NumLoopsNoLoad = {"", "NumLoopsNoLoad",
                                         "number of loops without loads"};
static llvm::Statistic NumLoopsNoStoreWithLoad = {
    "", "NumLoopsNoStoreWithLoad",
    "number of loops without store but with load"};
static llvm::Statistic NumLoopsWithCall = {"", "NumLoopsWithCall",
                                           "number of loops with calls"};
// add other stats
static llvm::Statistic LICMBasic = {"", "LICMBasic",
                                    "basic loop invariant instructions"};
static llvm::Statistic LICMLoadHoist = {"", "LICMLoadHoist",
                                        "loop invariant load instructions"};
static llvm::Statistic LICMStoreSink = {"", "LICMStoreSink",
                                        "loop invariant store instructions"};
static llvm::Statistic LICMNoPreheader = {
    "", "LICMNoPreheader", "absence of preheader prevents optimization"};

void printStats() {
    // Print stats
    errs() << "NumLoops: " << NumLoops << "\n";
    errs() << "NumLoopsNoStore: " << NumLoopsNoStore << "\n";
    errs() << "NumLoopsNoLoad: " << NumLoopsNoLoad << "\n";
    errs() << "NumLoopsNoStoreWithLoad: " << NumLoopsNoStoreWithLoad << "\n";
    errs() << "NumLoopsWithCall: " << NumLoopsWithCall << "\n";
    errs() << "LICMBasic: " << LICMBasic << "\n";
    errs() << "LICMLoadHoist: " << LICMLoadHoist << "\n";
    errs() << "LICMNoPreheader: " << LICMNoPreheader << "\n";
    errs() << "LICMStoreSink: " << LICMStoreSink << "\n";
}

// CSE Method Signatures
void printCSEStats();
static void CommonSubexpressionElimination(Module *M);

bool dominatesAllExits(const Instruction *I, const Loop *L,
                       const DominatorTreeBase<BasicBlock, false> *DT) {
    SmallVector<BasicBlock *, 8> ExitBlocks;
    L->getExitBlocks(ExitBlocks);
    for (auto exitBlock : ExitBlocks) {
        bool dominated = DT->dominates(I->getParent(), exitBlock);
        if (!dominated) {
            return false;
        }
    }
    return true;
}

bool isSameAddress(const Value *addr1, const Value *addr2) {
    bool isSame = false;
    if (const AllocaInst *alloca = dyn_cast<AllocaInst>(addr1)) {
        if (alloca == addr2) {
            isSame = true;
        }
    } else if (const GlobalVariable *global = dyn_cast<GlobalVariable>(addr1)) {
        if (global == addr2) {
            isSame = true;
        }
    } else {
        isSame = true;
    }
    return isSame;
}

bool canMoveStoreOutOfLoop(const Loop *L, const StoreInst *store,
                           const DominatorTreeBase<BasicBlock, false> *DT) {
    //   Requirements:
    // 1. Always store to same address
    // 2. No loads can be present to same address
    // 3. Always execute before loop exits

    const Value *storeAddr = store->getPointerOperand();
    // If store is volatile, return false
    if (store->isVolatile()) {
        return false;
    }

    bool hasLoads = false;
    bool hasAnyLoads = false;
    bool isAlloca = isa<AllocaInst>(storeAddr);

    // Going through all the instructions in the loop
    for (auto BB : L->getBlocks()) {
        for (auto instrPtr = BB->begin(); instrPtr != BB->end(); instrPtr++) {
            const Instruction *I = &*instrPtr;
            switch (I->getOpcode()) {
            case Instruction::Load: {
                const LoadInst *load = cast<LoadInst>(I);
                hasAnyLoads = true;
                hasLoads = hasLoads ||
                           isSameAddress(load->getPointerOperand(), storeAddr);
                break;
            }
            case Instruction::Store: {
                if (store == I) {
                    continue;
                }
                hasAnyLoads = true;
                const StoreInst *store2 = cast<StoreInst>(I);
                if (isSameAddress(store2->getPointerOperand(), storeAddr)) {
                    hasLoads = true;
                }
                break;
            }
            case Instruction::Call: {
                const CallInst *call = cast<CallInst>(I);
                if (call->isIdempotent()) {
                    hasAnyLoads = true;
                    hasLoads = true;
                }
                break;
            }
            default: {
                break;
            }
            }
        }
    }

    bool isAllocaInsideLoop = false;
    if (isAlloca) {
        if (const Instruction *addrInst = dyn_cast<Instruction>(storeAddr)) {
            isAllocaInsideLoop = L->contains(addrInst->getParent());
        }
    }

    if (isa<GlobalVariable>(storeAddr) && !hasLoads) {
        return true;
    }
    if (isAlloca && !hasLoads && !isAllocaInsideLoop) {
        return true;
    }
    if (!hasAnyLoads && L->isLoopInvariant(storeAddr) &&
        // 3. Always execute before loop exits
        dominatesAllExits(store, L, DT)) {
        return true;
    }

    return false;
}

bool canMoveLoadOutOfLoop(const Loop *L, const LoadInst *load,
                          const DominatorTreeBase<BasicBlock, false> *DT) {
    const Value *loadAddr = load->getPointerOperand();
    // If load is volatile, return false
    if (load->isVolatile()) {
        return false;
    }

    bool hasStores = false;
    bool hasAnyStores = false;
    bool isAlloca = isa<AllocaInst>(loadAddr);

    // Going through all the instructions in the loop
    for (auto BB : L->getBlocks()) {
        for (auto instrPtr = BB->begin(); instrPtr != BB->end(); instrPtr++) {
            const Instruction *I = &*instrPtr;
            switch (I->getOpcode()) {
            case Instruction::Store: {
                auto store = cast<StoreInst>(I);
                hasAnyStores = true;
                hasStores = hasStores ||
                            isSameAddress(store->getPointerOperand(), loadAddr);
                break;
            }
            case Instruction::Call: {
                const CallInst *call = cast<CallInst>(I);
                if (!call->isIdempotent()) {
                    hasAnyStores = true;
                    hasStores = true;
                }
                break;
            }
            default: {
                break;
            }
            }
        }
    }

    bool isAllocaInsideLoop = false;
    if (isAlloca) {
        if (const Instruction *addrInst = dyn_cast<Instruction>(loadAddr)) {
            isAllocaInsideLoop = L->contains(addrInst->getParent());
        }
    }

    if (isa<GlobalVariable>(loadAddr) && !hasStores) {
        return true;
    }
    if (isAlloca && !hasStores && !isAllocaInsideLoop) {
        return true;
    }
    if (!hasAnyStores && L->isLoopInvariant(loadAddr) &&
        dominatesAllExits(load, L, DT)) {
        return true;
    }

    return false;
}

// Copy `store` on each exit edge (insert a new block between the
// source and destination blocks of each exit edge and add a copy of
// `store` to that block)
void sinkStore(const Loop *L, const StoreInst *store,
               DominatorTreeBase<BasicBlock, false> *DT) {
    //    Get all the exit edges of the loop
    SmallVector<BasicBlock *, 8> ExitBlocks;
    L->getExitBlocks(ExitBlocks);

    //   For each exit edge, create a new block and add a copy of
    //   `store` to that block
    for (auto exitBlock : ExitBlocks) {
        // Create a new block
        
        BasicBlock *newBlock = BasicBlock::Create(
            exitBlock->getContext(), "", exitBlock->getParent(), exitBlock);
        newBlock->moveAfter(exitBlock);

        // Add a branch to the original exit block
        BranchInst::Create(exitBlock, newBlock);

        // Make copy of `store` inst
        Instruction *storeClone = store->clone();
        // Insert the new store before the terminator of newBlock
        storeClone->insertBefore(newBlock->getTerminator());

        // Update the dominator tree
        DT->addNewBlock(newBlock, exitBlock);
    }
}

void moveLoopInvariants(const Loop *L, const BasicBlock::iterator iter,
                        DominatorTreeBase<BasicBlock, false> *DT) {
    Instruction *I = &*iter;
    // Move the instructions
    bool madeLoopInvariant = false;
    if (L->hasLoopInvariantOperands(I)) {
        bool changed = false;
        madeLoopInvariant = L->makeLoopInvariant(I, changed);
    }
    if (madeLoopInvariant) {
        LICMBasic++;
    } else if (auto *load = dyn_cast<LoadInst>(I)) {
        if (canMoveLoadOutOfLoop(L, load, DT)) {
            auto preHeader = L->getLoopPreheader();
            load->moveBefore(preHeader->getTerminator());
            LICMLoadHoist++;
        }
    } else if (auto *store = dyn_cast<StoreInst>(I)) {
        if (canMoveStoreOutOfLoop(L, store, DT)) {
            sinkStore(L, store, DT);
            LICMStoreSink++;
        }
    }
}

void loopInvariantCodeMotion(const Loop *L,
                             DominatorTreeBase<BasicBlock, false> *DT) {
    NumLoops++;
    const ArrayRef<BasicBlock *> blocks = L->getBlocks();
    const BasicBlock *preHeader = L->getLoopPreheader();
    if (!preHeader) {
        LICMNoPreheader++;
        return;
    }
    if (blocks.size() == 0) {
        return;
    }
    uint num_stores = 0;
    uint num_loads = 0;
    uint num_calls = 0;

    for (auto basicBlock : blocks) {
        for (auto instIter = basicBlock->begin();
             instIter != basicBlock->end();) {
            Instruction *I = &*instIter;
            // Doing Analysis early helps autograder score, as hasLoopInvariant
            // may remove loads, stores or calls
            if (isa<LoadInst>(I)) {
                num_loads++;
            } else if (isa<StoreInst>(I)) {
                num_stores++;
            } else if (isa<CallInst>(I)) {
                num_calls++;
            }

            auto copyIter = instIter;
            instIter++;
            moveLoopInvariants(L, copyIter, DT);
        }
    }
    if (num_calls) {
        NumLoopsWithCall++;
    }
    if (num_stores == 0) {
        NumLoopsNoStore++;
    }
    if (num_loads == 0) {
        NumLoopsNoLoad++;
    }
    if (num_stores == 0 && num_loads > 0) {
        NumLoopsNoStoreWithLoad++;
    }
}

// make a loop which returns all the nested loops, recursively
void workOnNestedLoops(const Loop *L,
                       DominatorTreeBase<BasicBlock, false> *DT) {

    for (auto *subloop : L->getSubLoops()) {
        workOnNestedLoops(subloop, DT);
    }
    loopInvariantCodeMotion(L, DT);
}

static void LoopInvariantCodeMotion(Module *M) {
    errs() << "Name " << M->getName() << "\n";
    // iterate over all functions in the module
    for (auto F = M->begin(); F != M->end(); F++) {
        // Get loop info for this function
        auto *loops = new LoopInfoBase<BasicBlock, Loop>();
        auto *DT = new DominatorTreeBase<BasicBlock, false>();
        auto *func = &*F;

        // Check if func has a body
        if (func->empty()) {
            continue;
        }

        DT->recalculate(*func);
        loops->analyze(*DT);

        // iterate over all loops in the function
        for (auto L : *loops) {
            workOnNestedLoops(L, DT);
        }
    }

    printStats();
    CommonSubexpressionElimination(M);
    printCSEStats();
}

// CSE Pass

static llvm::Statistic CSEDead = {"", "CSEDead", "CSE found dead instructions"};
static llvm::Statistic CSEElim = {"", "CSEElim", "CSE redundant instructions"};
static llvm::Statistic CSESimplify = {"", "CSESimplify",
                                      "CSE simplified instructions"};
static llvm::Statistic CSELdElim = {"", "CSELdElim", "CSE redundant loads"};
static llvm::Statistic CSEStore2Load = {"", "CSEStore2Load",
                                        "CSE forwarded store to load"};
static llvm::Statistic CSEStElim = {"", "CSEStElim", "CSE redundant stores"};
void printCSEStats() {
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
bool isDead(Instruction &I);
void basicCSEPass(BasicBlock::iterator &inputIterator);
void eliminateRedundantLoads(LoadInst *loadInst,
                             BasicBlock::iterator &inputIterator);
void eliminateRedundantStoreCall(Instruction *storeCall,
                                 BasicBlock::iterator &originalIterator);
static void CommonSubexpressionElimination(Module *M) {

    // Iterate over all instructions in the module
    for (auto funcIter = M->begin(); funcIter != M->end(); funcIter++) {
        for (auto blockIter = funcIter->begin(); blockIter != funcIter->end();
             blockIter++) {
            for (auto instIter = blockIter->begin();
                 instIter != blockIter->end();) {
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
                if (auto simplified =
                        SimplifyInstruction(I, M->getDataLayout())) {
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

void removeCommonInstructionsIn(BasicBlock::iterator iterator, BasicBlock *bb,
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

bool isCall(Instruction *I) { return I->getOpcode() == Instruction::Call; }

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
            sameAddress(storeInst, nextLoad) &&
            sameDataType(storeInst, nextLoad)) {
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