#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Pass.h"
#include "llvm/R2COptions/R2COptions.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <queue>

using namespace llvm;
using namespace r2c;

#define DEBUG_TYPE "shuffle-functions"

// TODO: this pass is not target specific, move to non target-specific passes
namespace {
class ShuffleFunctionsPass : public ModulePass {
public:
  static char ID;
  ShuffleFunctionsPass() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    ModulePass::getAnalysisUsage(AU);
  }

  bool runOnModule(Module &M) override {
    bool Modified = false;

    if (r2c::ShuffleFunctions) {
      LLVM_DEBUG(dbgs() << "Before shuffling: \n"; {
        for (auto &Fn : M.getFunctionList()) {
          dbgs() << Fn.getName() << "\n";
        }
      });
      M.createRNG(this)->shuffle(M.getFunctionList());
      LLVM_DEBUG(dbgs() << "After shuffling: \n"; {
        for (auto &Fn : M.getFunctionList()) {
          dbgs() << Fn.getName() << "\n";
        }
      });
      Modified = true;
    }

    return Modified;
  }

  StringRef getPassName() const override { return "Shuffle Functions pass"; }
};
char ShuffleFunctionsPass::ID = 0;
} // End anonymous namespace.

namespace llvm {
ModulePass *createShuffleFunctionsPass() { return new ShuffleFunctionsPass(); }
} // namespace llvm