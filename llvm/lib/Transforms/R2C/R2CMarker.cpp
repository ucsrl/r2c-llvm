#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/InitializePasses.h>
#include <llvm/R2COptions/R2COptions.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <queue>

using namespace llvm;
using namespace r2c;

#define DEBUG_TYPE "r2c-marker"

namespace {

struct R2CMarker : public ModulePass {

  static char ID; // Pass identification, replacement for typeid
  R2CMarker() : ModulePass(ID) {
    initializeR2CMarkerPass(*PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override {
    if (Implementation.hasFlag(DisableDiversity)) {
      return false;
    }

    bool Changed = false;

    auto *GlobalAnnotations = M.getNamedGlobal("llvm.global.annotations");
    if (GlobalAnnotations) {
      auto *Array = cast<ConstantArray>(GlobalAnnotations->getOperand(0));
      for (size_t i = 0; i < Array->getNumOperands(); i++) {
        auto *Struct = cast<ConstantStruct>(Array->getOperand(i));
        if (auto *Fn =
                dyn_cast<Function>(Struct->getOperand(0)->getOperand(0))) {
          auto Annotation =
              cast<ConstantDataArray>(
                  cast<GlobalVariable>(Struct->getOperand(1)->getOperand(0))
                      ->getOperand(0))
                  ->getAsCString();
          Fn->addFnAttr(Annotation); // <-- add function annotation here
        }
      }
    }

    for (auto &F : M) {
      if (!F.isDeclaration()) {
        if (!F.hasFnAttribute("no_btra")) {
          F.addFnAttr("diversity-btra-candidate", llvm::toStringRef(true));
          Changed = true;
        }
      }
    }

    return Changed;
  }
};
} // namespace

namespace llvm {
ModulePass *createR2CMarkerPass() { return new R2CMarker(); }
} // namespace llvm

char R2CMarker::ID = 0;

INITIALIZE_PASS_BEGIN(R2CMarker, "r2cmarkerpass", "Mark R2C functions", false,
                      false)
INITIALIZE_PASS_END(R2CMarker, "r2cmarkerpass", "Mark R2C functions", false,
                    false)