//
// Created by felixl on 8/11/20.
//
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO.h"
#include <llvm/Analysis/BlockFrequencyInfo.h>
#include <llvm/Analysis/ProfileSummaryInfo.h>
#include <llvm/IR/AbstractCallSite.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/InitializePasses.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/R2COptions/R2COptions.h>

using namespace llvm;
using namespace r2c;

#define DEBUG_TYPE "collect-statistics"

STATISTIC(HotCallSitesWithColdFunction,
          "Number of hot call sites which call a cold function");
STATISTIC(HotFunctionsWithColdCallSite,
          "Number of hot functions which are called by a cold call site");
STATISTIC(HotIndirectCallSite,
          "Number of hot call sites with a statically unknown target");
STATISTIC(ColdIndirectCallSite,
          "Number of cold call sites with a statically unknown target");
STATISTIC(HotDirectCallSite,
          "Number of hot call sites with a statically known target");
STATISTIC(ColdDirectCallSite,
          "Number of cold call sites with a statically known target");
STATISTIC(ColdCallSites, "Number of cold call sites");
STATISTIC(HotCallSites, "Number of hot call sites");
STATISTIC(NumFunctions, "Number of functions");
STATISTIC(NumHotFunctions, "Number of functions with hot entry");
STATISTIC(NumColdFunctions, "Number of functions with cold entry");

namespace {

struct CollectStatistics : public ModulePass {
  static char ID; // Pass identification, replacement for typeid
  CollectStatistics() : ModulePass(ID) {
    initializeDiversityPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &Usage) const override;

  bool runOnModule(Module &M) override {
    if (!r2c::EnableStatistics)
      return false;

    for (auto &F : M) {
      auto PSI = &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
      NumFunctions++;
      if (PSI->isFunctionEntryCold(&F)) {
        NumColdFunctions++;
        F.addFnAttr("rad-cold-function");
      }

      if (PSI->isFunctionEntryHot(&F)) {
        NumHotFunctions++;
        F.addFnAttr("rad-hot-function");
      }

      for (auto &Block : F) {
        auto BFI = &getAnalysis<BlockFrequencyInfoWrapperPass>(F).getBFI();

        for (auto &Instruction : Block) {
          if (auto *Call = dyn_cast<CallBase>(&Instruction)) {
            auto &CS = *Call;
            bool isHot = PSI->isHotCallSite(CS, BFI);
            bool isCold = PSI->isColdCallSite(CS, BFI);

            if (isHot) {
              CS.addAttribute(AttributeList::FunctionIndex,
                              Attribute::get(CS.getContext(), "rad-hot-call"));
              HotCallSites++;
            }

            if (isCold) {
              CS.addAttribute(
                  AttributeList::FunctionIndex,
                  Attribute::get(CS.getContext(), "rad-cold-call"));
              ColdCallSites++;
            }

            if (CS.getCalledFunction() == nullptr) {
              if (isHot) {
                HotIndirectCallSite++;
              }

              if (isCold) {
                ColdIndirectCallSite++;
              }
            } else {
              if (isHot) {
                HotDirectCallSite++;
                if (PSI->isFunctionEntryCold(CS.getCalledFunction())) {
                  HotCallSitesWithColdFunction++;
                }
              }

              if (isCold) {
                ColdDirectCallSite++;
                if (PSI->isFunctionEntryHot(CS.getCalledFunction())) {
                  HotFunctionsWithColdCallSite++;
                }
              }
            }

          }
        }
      }
    }

    return true;
  }
};
} // namespace

ModulePass *llvm::createCollectStatisticsPass() {
  return new CollectStatistics();
}

char CollectStatistics::ID = 0;
void CollectStatistics::getAnalysisUsage(AnalysisUsage &Usage) const {
  Usage.addRequired<BlockFrequencyInfoWrapperPass>();
  Usage.addRequired<ProfileSummaryInfoWrapperPass>();
  Usage.getPreservesAll();
}

INITIALIZE_PASS_BEGIN(CollectStatistics, "collectstatistics",
                      "R2C Implementation Statistics", false, false)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(BlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_END(CollectStatistics, "collectstatistics",
                    "R2C Implementation Statistics", false, false)
