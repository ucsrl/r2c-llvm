#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Pass.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Transforms/R2C.h"
#include <llvm/Analysis/ProfileSummaryInfo.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/InitializePasses.h>
#include <llvm/R2COptions/R2COptions.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <queue>
#include <unordered_set>

using namespace llvm;
using namespace r2c;

#define DEBUG_TYPE "heapboobytrap-pass"

namespace {

struct HeapBoobyTrapPass : ModulePass {
  static char ID; // Pass identification, replacement for typeid

  HeapBoobyTrapPass() : ModulePass(ID) {}

  static void makeModuleLocal(GlobalVariable *GV) {
    GV->setLinkage(GlobalValue::WeakODRLinkage);
    GV->setVisibility(GlobalValue::HiddenVisibility);
  }

  StringRef getPassName() const override {
    return "Heap boobytrap insertion pass";
  }

  bool runOnModule(Module &M) override{
    if (r2c::MaxHeapPtrBoobyTraps > 0) {
      if (!RNG)
        RNG.reset(M.createRNG(this));
      std::string ArrayName =
          HardenHeapBoobyTraps ? "__heap_bt_array_ptr" : "__heap_bt_array0";

      bool NeedsBoobyTraps = false;
      for (auto &F : M) {
        if (!F.isDeclaration() && !F.hasFnAttribute("no_heapbt")) {
          if (r2c::MaxHeapPtrBoobyTraps > 0) {
            unsigned int HeapBoobyTraps =
                RNG->Random(r2c::MaxHeapPtrBoobyTraps + 1);
            F.addFnAttr("diversity-num-heap-bts", utostr(HeapBoobyTraps));
            F.addFnAttr("diversity-heap-bt-array", ArrayName);
            NeedsBoobyTraps = true;
          }
        }
      }

      if (!NeedsBoobyTraps) {
        return false;
      }

      std::string SizeName("__heap_bt_num");
      GlobalVariable *NumBTs = cast<GlobalVariable>(
          M.getOrInsertGlobal(SizeName, Type::getInt32Ty(M.getContext())));

      /*
       * Only one module should actually initialize the array -> weak
       * linkage. Shared libraries should use their *own* version of the data
       * though, without the possibility of ELF interposition and without shared
       * libraries using the booby trap array of the main executable. Using the
       * main executable's array would require an indirection via the GOT and
       * as a result an additional instruction. Visibility hidden tells the
       * dynamic linker that a shared library should access the data PC
       * relative, but without a GOT.
       */
      makeModuleLocal(NumBTs);
      NumBTs->setInitializer(ConstantInt::get(Type::getInt32Ty(M.getContext()),
                                              r2c::HeapPtrArraySize, false));
      Constant *Initializer;
      GlobalVariable *BoobyTrapArray;
      if (HardenHeapBoobyTraps) {
        LLVM_DEBUG(dbgs() << "Using hardened heap boobytraps\n");
        ArrayType *InitArrayT = ArrayType::get(Type::getInt64Ty(M.getContext()),
                                               r2c::HeapPtrArraySize);
        PointerType *ArrayPointerT = InitArrayT->getPointerTo();
        BoobyTrapArray = cast<GlobalVariable>(
            M.getOrInsertGlobal(ArrayName, ArrayPointerT));

        /* before being initialized by the runtime library, the
         * heap_bt_array_ptr points to a global array with dummy values. This
         * allows us to compile even allocator code (e.g. tcmalloc) with heap
         * boobytraps enabled, without causing an invalid access during the
         * initialization (the runtime libarary calls malloc to allocate the
         * array and the malloc prologue already wants to read from the
         * boobytrap array)
         */
        std::vector<Constant *> InitValues;
        for (unsigned int I = 0; I < r2c::HeapPtrArraySize; I++) {
          InitValues.push_back(
              ConstantInt::get(Type::getInt64Ty(M.getContext()), 0xCCCC));
        }
        GlobalVariable *InitArray = cast<GlobalVariable>(
            M.getOrInsertGlobal("__heap_bt_array_ptr_init", InitArrayT));
        makeModuleLocal(InitArray);
        InitArray->setInitializer(ConstantArray::get(InitArrayT, InitValues));

        Initializer = InitArray;

        // Setup the booby trap pointers protecting the array pointer itself
        std::string ArrayBoobyTrapsNum("__heap_bt_array_bt_num");
        GlobalVariable *NumArrayBTs = cast<GlobalVariable>(M.getOrInsertGlobal(
            ArrayBoobyTrapsNum, Type::getInt32Ty(M.getContext())));
        makeModuleLocal(NumArrayBTs);
        NumArrayBTs->setInitializer(
            ConstantInt::get(Type::getInt32Ty(M.getContext()),
                             r2c::MaxPtrArrayBoobyTraps, false));

        ArrayType *BTVariablePointerArrayType = ArrayType::get(
            Type::getInt64PtrTy(M.getContext()), r2c::MaxPtrArrayBoobyTraps);
        GlobalVariable *ArrayBoobyTrapsArray =
            static_cast<GlobalVariable *>(M.getOrInsertGlobal(
                "__heap_bt_array_bt_array", BTVariablePointerArrayType));
        makeModuleLocal(ArrayBoobyTrapsArray);
        std::vector<Constant *> BTVariablePointers;
        for (unsigned int I = 0; I < r2c::MaxPtrArrayBoobyTraps; I++) {
          GlobalVariable *BTVariable = new GlobalVariable(
              M, Type::getInt64Ty(M.getContext()), false,
              GlobalValue::WeakODRLinkage,
              ConstantInt::get(Type::getInt64Ty(M.getContext()), 0xBBBB, false),
              "ArrayPtrBT");
          makeModuleLocal(BTVariable);
          BTVariablePointers.push_back(BTVariable);
        }
        ArrayBoobyTrapsArray->setInitializer(
            ConstantArray::get(BTVariablePointerArrayType, BTVariablePointers));

      } else {
        LLVM_DEBUG(dbgs() << "Using non-hardened heap boobytraps\n");
        ArrayType *ArrayT = ArrayType::get(Type::getInt64Ty(M.getContext()),
                                       r2c::HeapPtrArraySize);
        BoobyTrapArray =
            cast<GlobalVariable>(M.getOrInsertGlobal(ArrayName, ArrayT));

        /*
         * Prefill with a constant integer value for debugging purposes
         */
        std::vector<Constant *> Values;
        for (unsigned int I = 0; I < r2c::HeapPtrArraySize; I++) {
          Values.push_back(
              ConstantInt::get(Type::getInt64Ty(M.getContext()), 0xAAAA));
        }
        Initializer = ConstantArray::get(ArrayT, Values);
      }

      makeModuleLocal(BoobyTrapArray);
      BoobyTrapArray->setInitializer(Initializer);
    }

    return true;
  }

  void getAnalysisUsage(AnalysisUsage &Usage) const override {
    Usage.getPreservesAll();
  }

private:
  std::unique_ptr<RandomNumberGenerator> RNG;
};
} // namespace

namespace llvm {
ModulePass *createHeapBoobyTrapPass() { return new HeapBoobyTrapPass(); }
} // namespace llvm

char HeapBoobyTrapPass::ID = 0;


INITIALIZE_PASS_BEGIN(HeapBoobyTrapPass, "heapboobytrappass",
                      "Heap Booby Traps", false, false)
INITIALIZE_PASS_END(HeapBoobyTrapPass, "heapboobytrappass", "Heap Booby Traps",
                    false, false)
