//===----- GlobalRandomization.cpp - Global Variable Randomization --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements random reordering and padding of global variables.
// The pass was taken almost as is from the multicompiler project
// https://github.com/securesystemslab/multicompiler
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "global-randomization"

#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <algorithm>
#include <llvm/Analysis/ProfileSummaryInfo.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/InitializePasses.h>
#include <llvm/R2COptions/R2COptions.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <queue>

using namespace llvm;
using namespace r2c;

//===----------------------------------------------------------------------===//
//                           GlobalRandomization Pass
//===----------------------------------------------------------------------===//

namespace {

class GlobalRandomization : public ModulePass {
  std::unique_ptr<RandomNumberGenerator> RNG;

public:
  virtual bool runOnModule(Module &M);

public:
  static char ID; // Pass identification, replacement for typeid.
  GlobalRandomization() : ModulePass(ID) {}

private:
  GlobalVariable *CreatePadding(GlobalVariable::LinkageTypes linkage,
                                GlobalVariable *G = nullptr);

  Module *CurModule;
};
} // namespace

char GlobalRandomization::ID = 0;

static int compareNames(Constant *const *A, Constant *const *B) {
  return (*A)->getName().compare((*B)->getName());
}

static void setUsedInitializer(GlobalVariable *V, Module &M,
                               const SmallPtrSet<GlobalValue *, 8> &Init) {
  if (Init.empty()) {
    if (V)
      V->eraseFromParent();
    return;
  }

  // Type of pointer to the array of pointers.
  PointerType *Int8PtrTy = Type::getInt8PtrTy(M.getContext(), 0);

  SmallVector<llvm::Constant *, 8> UsedArray;
  for (GlobalValue *GV : Init) {
    Constant *Cast =
        ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, Int8PtrTy);
    UsedArray.push_back(Cast);
  }
  // Sort to get deterministic order.
  array_pod_sort(UsedArray.begin(), UsedArray.end(), compareNames);
  ArrayType *ATy = ArrayType::get(Int8PtrTy, UsedArray.size());

  if (V)
    V->removeFromParent();
  GlobalVariable *NV =
      new GlobalVariable(M, ATy, false, llvm::GlobalValue::AppendingLinkage,
                         llvm::ConstantArray::get(ATy, UsedArray), "");
  if (V)
    NV->takeName(V);
  else
    NV->setName("llvm.used");
  NV->setSection("llvm.metadata");
  if (V)
    delete V;
}

GlobalVariable *
GlobalRandomization::CreatePadding(GlobalVariable::LinkageTypes linkage,
                                   GlobalVariable *G) {
  Type *Int8Ty = Type::getInt8Ty(CurModule->getContext());

  unsigned Size = RNG->Random(r2c::GlobalPaddingMaxSize - 1) + 1;
  ArrayType *PaddingType = ArrayType::get(Int8Ty, Size);
  Constant *Init;
  if (!G || G->getInitializer()->isZeroValue()) {
    Init = ConstantAggregateZero::get(PaddingType);
  } else {
    SmallVector<Constant *, 32> PaddingInit(Size,
                                            ConstantInt::get(Int8Ty, 0xff));
    Init = ConstantArray::get(PaddingType, PaddingInit);
  }

  return new GlobalVariable(*CurModule, PaddingType, false, linkage, Init,
                            "[padding]", G);
}

template <typename T> void reverse(SymbolTableList<T> &list) {
  if (list.empty())
    return;

  // using std::reverse directly on an iplist<T> would be simpler
  // but isn't supported.
  SmallVector<T *, 10> rlist;
  for (typename SymbolTableList<T>::iterator i = list.begin();
       i != list.end();) {
    // iplist<T>::remove increments the iterator which is why
    // the for loop doesn't.
    T *t = list.remove(i);
    rlist.push_back(t);
  }

  std::reverse(rlist.begin(), rlist.end());

  for (typename SmallVector<T *, 10>::size_type i = 0; i < rlist.size(); i++) {
    list.push_back(rlist[i]);
  }
}

bool GlobalRandomization::runOnModule(Module &M) {
  if (!r2c::ShuffleGlobals && r2c::GlobalPaddingPercentage == 0)
    return false;

  CurModule = &M;

  if (!RNG)
    RNG.reset(M.createRNG(this));

  Module::GlobalListType &Globals = M.getGlobalList();

  SmallVector<GlobalVariable *, 10> WorkList;
  SmallPtrSet<GlobalValue *, 8> UsedGlobals;
  GlobalVariable *UsedV = collectUsedGlobalVariables(M, UsedGlobals, false);

  for (GlobalVariable &G : Globals) {
    if (G.hasInitializer() && !G.isConstant())
      WorkList.push_back(&G);
  }

  unsigned long NormalGlobalCount = 0;
  unsigned long CommonGlobalCount = 0;
  for (GlobalVariable *G : WorkList) {
    GlobalVariable::LinkageTypes linkage = GlobalVariable::InternalLinkage;
    if (G->hasCommonLinkage()) {
      linkage = GlobalVariable::CommonLinkage;
      CommonGlobalCount++;
    } else {
      NormalGlobalCount++;
    }

    if (r2c::GlobalPaddingPercentage == 0)
      continue;

    unsigned Roll = RNG->Random(100);
    if (Roll >= r2c::GlobalPaddingPercentage)
      continue;

    // Insert padding
    UsedGlobals.insert(CreatePadding(linkage, G));
  }

  // Increase the number of globals to increase the entropy of their layout.
  if (NormalGlobalCount > 0)
    for (; NormalGlobalCount < r2c::GlobalMinCount; ++NormalGlobalCount)
      UsedGlobals.insert(CreatePadding(GlobalVariable::InternalLinkage));

//  if (CommonGlobalCount > 0)
//    for (; CommonGlobalCount < multicompiler::GlobalMinCount; ++CommonGlobalCount)
//      UsedGlobals.insert(CreatePadding(GlobalVariable::CommonLinkage));

  setUsedInitializer(UsedV, M, UsedGlobals);

  // Global variable randomization
  if (r2c::ShuffleGlobals) {
    RNG->shuffle(Globals);
    LLVM_DEBUG(dbgs() << "shuffled order of " << Globals.size()
                      << " global variables\n");
  }

  // Dump globals after randomization and reversal. Note: linker may affect this
  // order.
  LLVM_DEBUG(dbgs() << "start list of randomized global variables\n");
  for (GlobalVariable &G : Globals) {
    LLVM_DEBUG(G.dump());
  }
  LLVM_DEBUG(dbgs() << "end list of randomized global variables\n");

  return true;
}

namespace llvm {
ModulePass *createGlobalRandomizationPass() {
  return new GlobalRandomization();
}
} // namespace llvm

INITIALIZE_PASS_BEGIN(GlobalRandomization, "global-randomization",
                      "Randomize global variables", false, false)
INITIALIZE_PASS_END(GlobalRandomization, "global-randomization",
                    "Randomize global variables", false, false)
