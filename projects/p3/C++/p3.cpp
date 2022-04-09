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

using namespace llvm;

static void LoopInvariantCodeMotion(Module *);

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

bool canMoveOutOfLoop(Loop *L, LoadInst *load) {
    if (load->isVolatile())
        return false;
    Value *loadAddr = load->getPointerOperand();
    
    bool isAddressGlobal = isa<GlobalVariable>(loadAddr);
    bool isAddressAlloca = isa<AllocaInst>(loadAddr);
    bool isAddressLoopInvariant = L->isLoopInvariant(loadAddr);
    bool hasStoreInLoop = false;
    bool addressAllocaInLoop = false; // TODO

    // Check if there are users of the loadAddr in the loop
    for (auto u = loadAddr->user_begin(); u != loadAddr->user_end(); u++) {
        Instruction *user = cast<Instruction>(*u);
        if (L->contains(user)) {
            switch (user->getOpcode()) {
            case Instruction::Store:
                hasStoreInLoop = true;
                if (cast<StoreInst>(user)->getPointerOperand() == loadAddr) {
                } else {
                    errs() << "The store should be having same addres, why is it different. The instruction is:\n";
                    errs() << *user << "\n";
                    errs() << "The load is:\n";
                    errs() << *load << "\n";
                    errs() << "The loop is:\n";
                    errs() << *L << "\n";
                    errs() << "__END__\n";
                }
                // hasStoreInLoop = true;
                break;
            case Instruction::Alloca:
                // TODO: Is this the correct way? Does Alloca come in def-use?
                addressAllocaInLoop = true;
                errs() << "\n";
                break;
            default:
                break;
            }

        }
    }

    // if (addr is a GlobalVariable and there are no possible stores to addr in L):
    if (isAddressGlobal && !hasStoreInLoop) {
        return true;
    }

    // if (addr is an AllocaInst and no possible stores to addr in L and AllocaInst is not inside the loop):
    if (isAddressAlloca && !hasStoreInLoop && addressAllocaInLoop) {
        return false;
    }

    // if (there are no possible stores to any addr in L && addr is loop invariant && I dominates L’s exit):
    // TODO: check if I dominates L’s exit
    if (!hasStoreInLoop && isAddressLoopInvariant && true) { 
        return true;
    }
    return false;
}

void loopInvariantCodeMotion(Loop *loop) {
    NumLoops++;
    auto preHeader = loop->getLoopPreheader();
    if (!preHeader) {
        LICMNoPreheader++;
        return;
    }
    uint num_stores = 0;
    uint num_loads = 0;
    uint num_calls = 0;

    auto blocks = loop->getBlocks();
    if (blocks.size() == 0) {
        return;
    }

    for (auto basicBlock : blocks) {
        for (auto instrPtr = basicBlock->begin();
             instrPtr != basicBlock->end();) {
            Instruction *I = &*instrPtr;
            instrPtr++;
            if (loop->hasLoopInvariantOperands(I)) {
                bool madeLoopInvariant = false;
                loop->makeLoopInvariant(I, madeLoopInvariant);
                LICMBasic++;
            } else if (auto load = dyn_cast<LoadInst>(I)) {
                // num_loads++;
                // if (canMoveOutOfLoop(loop, load)) {
                //     // Move I to preHeader
                //     I->moveBefore(preHeader->getTerminator());
                //     LICMLoadHoist++;
                // }
            } else if (auto store = dyn_cast<StoreInst>(I)) {
                // num_stores++;
            } else if (auto call = dyn_cast<CallInst>(I)) {
                // num_calls++;
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
    // TODO: Do we need to check if the stores and loads are to the same loadAddr?
    if (num_stores == 0 && num_loads > 0) {
        NumLoopsNoStoreWithLoad++;
    }
}

static void LoopInvariantCodeMotion(Module *M) {
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
        for (auto loop : *loops) {
            loopInvariantCodeMotion(loop);
        }
    }
}
