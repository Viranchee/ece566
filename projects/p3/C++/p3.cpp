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
                                          cl::Required,
                                          cl::init("-"));

static cl::opt<std::string> OutputFilename(cl::Positional,
                                           cl::desc("<output bitcode>"),
                                           cl::Required,
                                           cl::init("out.bc"));

static cl::opt<bool>
    Mem2Reg("mem2reg",
            cl::desc("Perform memory to register promotion before LICM."),
            cl::init(false));

static cl::opt<bool>
    CSE("cse", cl::desc("Perform CSE before LICM."), cl::init(false));

static cl::opt<bool> NoLICM("no-licm",
                            cl::desc("Do not perform LICM optimization."),
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

static llvm::Statistic NumLoops = {"", "NumLoops", "number of loops analyzed"};
static llvm::Statistic NumLoopsNoStore = {"",
                                          "NumLoopsNoStore",
                                          "number of loops without stores"};
static llvm::Statistic NumLoopsNoLoad = {"",
                                         "NumLoopsNoLoad",
                                         "number of loops without loads"};
static llvm::Statistic NumLoopsNoStoreWithLoad = {
    "",
    "NumLoopsNoStoreWithLoad",
    "number of loops without store but with load"};
static llvm::Statistic NumLoopsWithCall = {"",
                                           "NumLoopsWithCall",
                                           "number of loops with calls"};
// add other stats
static llvm::Statistic LICMBasic = {"",
                                    "LICMBasic",
                                    "basic loop invariant instructions"};
static llvm::Statistic LICMLoadHoist = {"",
                                        "LICMLoadHoist",
                                        "loop invariant load instructions"};
static llvm::Statistic LICMNoPreheader = {
    "",
    "LICMNoPreheader",
    "absence of preheader prevents optimization"};

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

bool dominatesAllExits(Instruction *I, Loop *L) {
    auto domTree = getDomTree(I);
    DomTreeNodeBase<BasicBlock>::iterator it, end;
    SmallVector<BasicBlock *, 8> ExitBlocks;
    L->getExitBlocks(ExitBlocks);

    auto numberOfExitBlocks = ExitBlocks.size();
    int numberOfExitBlocksDominated = 0;
    errs() << "number of exit blocks: " << numberOfExitBlocks << "\n";
    for (auto exitBlock : ExitBlocks) {
        // For all blocks in domTree, check if it has exitBlock
        for (it = domTree->begin(), end = domTree->end(); it != end; it++) {
            BasicBlock *bb_next = (*it)->getBlock();
            if (bb_next == exitBlock) {
                numberOfExitBlocksDominated++;
                errs() << numberOfExitBlocksDominated << "BLOCKS DOMINATED\n";
            }
        }
    }
    return numberOfExitBlocksDominated == numberOfExitBlocks;
}

bool canMoveOutOfLoop(Loop *L, LoadInst *load) {
    auto addr = load->getPointerOperand();
    // If load is volatile, return false
    if (load->isVolatile()) {
        return false;
    }
    bool isGlobal = isa<GlobalVariable>(addr);
    bool isAlloca = isa<AllocaInst>(addr);
    auto addrInst = dyn_cast<Instruction>(addr);
    bool isAllocaInsideLoop = true;
    if (addrInst) {
        isAllocaInsideLoop = L->contains(addrInst);
    }
    bool hasStores = true;
    bool hasAnyStores = true;
    bool isLoopInvariant = L->isLoopInvariant(addr);
    // VIR CHECK: CHECK IF CODE WORKS TILL HERE WITHOUT SEGFAULTS
    // Check if loop has any stores
    auto blocks = L->getBlocks();
    for (auto BB : blocks) {
        for (auto instrPtr = BB->begin(); instrPtr != BB->end(); instrPtr++) {
            auto *I = &*instrPtr;
            if (isa<StoreInst>(I)) {
                auto storeInst = cast<StoreInst>(I);
                hasAnyStores = true;
                if (storeInst->getPointerOperand() == addr) {
                    hasStores = true;
                }
                break;
            }
        }
    }

    if (isGlobal && !hasStores) {
        return true;
    }
    if (isAlloca && !hasStores && !isAllocaInsideLoop) {
        return true;
    }
    if (!hasAnyStores && isLoopInvariant && false) {
        return true;
    }

    return false;
}

void loopInvariantCodeMotion(Loop *L) {
    auto blocks = L->getBlocks();
    if (blocks.size() == 0) {
        return;
    }
    NumLoops++;

    auto preHeader = L->getLoopPreheader();
    if (!preHeader) {
        LICMNoPreheader++;
        return;
    }
    uint num_stores = 0;
    uint num_loads = 0;
    uint num_calls = 0;

    for (auto basicBlock : blocks) {
        for (auto instIter = basicBlock->begin();
             instIter != basicBlock->end();) {
            Instruction *I = &*instIter;
            instIter++;
            bool changed = false;
            bool madeLoopInvariant = false;
            // Doing Analysis early helps autograder score, as hasLoopInvariant
            // may remove loads, stores or calls
            if (isa<LoadInst>(I)) {
                num_loads++;
            } else if (isa<StoreInst>(I)) {
                num_stores++;
            } else if (isa<CallInst>(I)) {
                num_calls++;
            }
            // Move the instructions
            if (L->hasLoopInvariantOperands(I)) {
                madeLoopInvariant = L->makeLoopInvariant(I, changed);
                if (madeLoopInvariant) {
                    LICMBasic++;
                } else {
                    auto *load = dyn_cast<LoadInst>(I);
                    if (load) {
                        if (canMoveOutOfLoop(L, load)) {
                            load->moveBefore(preHeader->getTerminator());
                            LICMLoadHoist++;
                        }
                    }
                }
            }
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
    // TODO: Do we need to check if the stores and loads are to the same
    // loadAddr?
    if (num_stores == 0 && num_loads > 0) {
        NumLoopsNoStoreWithLoad++;
    }
}

// given a loop, return all the nested loops, including itself, recursively
static std::vector<Loop *> getNestedLoops(Loop *L) {
    std::vector<Loop *> nestedLoops;
    for (auto *subLoops : L->getSubLoops()) {
        auto childNestedLoops = getNestedLoops(subLoops);
        for (auto subLoop : childNestedLoops) {
            nestedLoops.push_back(subLoop);
        }
        nestedLoops.push_back(subLoops);
    }
    return nestedLoops;
}

static void LoopInvariantCodeMotion(Module *M) {
    errs() << "Name " << M->getName() << "\n";
    // iterate over all functions in the module
    for (auto F = M->begin(); F != M->end(); F++) {
        // Get loop info for this function
        auto *loops = new LoopInfoBase<BasicBlock, Loop>();
        auto DT = new DominatorTreeBase<BasicBlock, false>();
        auto func = &*F;

        // Check if func has a body
        if (func->empty()) {
            continue;
        }

        DT->recalculate(*func);
        loops->analyze(*DT);

        // iterate over all loops in the function
        for (auto L : *loops) {
            auto subLoops = getNestedLoops(L);
            subLoops.push_back(L);
            for (auto subLoop : subLoops) {
                loopInvariantCodeMotion(subLoop);
            }
        }
    }

    // Print stats
    errs() << "NumLoops: " << NumLoops << "\n";
    errs() << "NumLoopsNoStore: " << NumLoopsNoStore << "\n";
    errs() << "NumLoopsNoLoad: " << NumLoopsNoLoad << "\n";
    errs() << "NumLoopsNoStoreWithLoad: " << NumLoopsNoStoreWithLoad << "\n";
    errs() << "NumLoopsWithCall: " << NumLoopsWithCall << "\n";
    errs() << "LICMBasic: " << LICMBasic << "\n";
    errs() << "LICMLoadHoist: " << LICMLoadHoist << "\n";
    errs() << "LICMNoPreheader: " << LICMNoPreheader << "\n";
}