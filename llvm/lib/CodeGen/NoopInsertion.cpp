//===- NoopInsertion.cpp - Noop Insertion ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass adds fine-grained diversity by displacing code using randomly
// placed (optionally target supplied) Noop instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/NoopInsertion.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LazyMachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include <llvm/Analysis/ProfileSummaryInfo.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/InitializePasses.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <queue>
#include <unordered_set>

using namespace llvm;

#define DEBUG_TYPE "noop-insertion"

static cl::opt<unsigned> NoopInsertionPercentage(
    "noop-insertion-percentage",
    cl::desc("Percentage of instructions that have Noops prepended"),
    cl::init(25)); // Default is a good balance between entropy and
                   // performance impact

static cl::opt<unsigned> NoopInsertionLowPercentage(
    "noop-insertion-low-percentage",
    cl::desc("Lower probability of inserting a Noop when PGO is used"),
    cl::init(25)); // Default is a good balance between entropy and
                   // performance impact

static cl::opt<unsigned> MaxNoopsPerInstruction(
    "max-noops-per-instruction",
    llvm::cl::desc("Maximum number of Noops per instruction"),
    llvm::cl::init(1));

static cl::opt<bool> NoopUsePGO(
    "noop-use-pgo",
    llvm::cl::desc(
        "Use PGO support to determine the number o noops. "
        "noop-insertion-percentage will determine the upper probability bound "
        "and noop-insertion-lower-percentage the lower bound."),
    llvm::cl::init(0));

STATISTIC(InsertedNoops,
          "Total number of noop type instructions inserted for diversity");

char NoopInsertion::ID = 0;
char &llvm::NoopInsertionID = NoopInsertion::ID;
INITIALIZE_PASS(NoopInsertion, "noop-insertion",
                "Noop Insertion for fine-grained code randomization", false,
                false)

NoopInsertion::NoopInsertion() : MachineFunctionPass(ID) {
  initializeNoopInsertionPass(*PassRegistry::getPassRegistry());

  // clamp percentage to 100
  if (NoopInsertionPercentage > 100)
    NoopInsertionPercentage = 100;
}

void NoopInsertion::getAnalysisUsage(AnalysisUsage &AU) const {
  if (NoopUsePGO) {
    AU.addRequired<LazyMachineBlockFrequencyInfoPass>();
    AU.addRequired<ProfileSummaryInfoWrapperPass>();
  }
  AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool NoopInsertion::runOnMachineFunction(MachineFunction &Fn) {
  // The RNG must be initialized on first use so we have a Module to
  // construct it from
  if (!RNG)
    RNG.reset(Fn.getFunction().getParent()->createRNG(this));

  if (NoopUsePGO) {
    BFI =
        &getAnalysis<LazyMachineBlockFrequencyInfoPass>().getBFI();
    if (!Summary) {
      Summary.reset(ProfileSummary::getFromMD(
          Fn.getFunction().getParent()->getProfileSummary(false)));
    }
  }

  const TargetInstrInfo *TII = Fn.getSubtarget().getInstrInfo();

  unsigned FnInsertedNoopCount = 0;

  for (auto &BB : Fn) {
    MachineBasicBlock::iterator FirstTerm = BB.getFirstTerminator();

    unsigned Prob;
    if (NoopUsePGO) {
      Optional<uint64_t> BlockCountOpt =
          BFI->getBlockProfileCount(&BB);
      if (!BlockCountOpt.hasValue()) {
        Prob = NoopInsertionPercentage;
      } else {
        unsigned MaxCount = Summary->getMaxCount();
        unsigned BlockCount = BlockCountOpt.getValue();
        Prob = NoopInsertionPercentage -
               (NoopInsertionPercentage - NoopInsertionLowPercentage) *
                   (log10(1 + BlockCount) / log10(1 + MaxCount));
        LLVM_DEBUG(if (Prob != NoopInsertionPercentage) dbgs() << "Using reduced probability " << Prob << "\n");
      }
    } else {
      Prob = NoopInsertionPercentage;
    }

    for (MachineBasicBlock::iterator I = BB.begin(), E = BB.end(); I != E;
         ++I) {
      if (I->isPseudo())
        continue;

      // Insert random number of Noop-like instructions.
      for (unsigned i = 0; i < MaxNoopsPerInstruction; i++) {
        if (RNG->Random(100) >= Prob)
          continue;

        TII->insertNoop(BB, I, *RNG);

        ++FnInsertedNoopCount;
      }

      if (I == FirstTerm)
        break;
    }
  }

  InsertedNoops += FnInsertedNoopCount;

  return FnInsertedNoopCount > 0;
}