#include "X86InstrInfo.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Pass.h"
#include "llvm/R2COptions/R2COptions.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <queue>

using namespace llvm;

#define DEBUG_TYPE "prolog-padding"

static cl::opt<unsigned> MaxPrologPaddingInstructions(
    "prolog-max-padding-instructions",
    llvm::cl::desc(
        "Maximum number of trap instructions to use for prolog padding"),
    llvm::cl::init(0));

static cl::opt<unsigned> MinPrologPaddingInstructions(
    "prolog-min-padding-instructions",
    llvm::cl::desc(
        "Minimum number of trap instructions to use for prolog padding"),
    llvm::cl::init(0));

namespace {
class X86PrologPadding : public MachineFunctionPass {
public:
  static char ID;
  X86PrologPadding() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (MinPrologPaddingInstructions == 0 && MaxPrologPaddingInstructions == 0)
      return false;

    // The RNG must be initialized on first use so we have a Module to
    // construct it from
    if (!RNG) {
      RNG.reset(MF.getFunction().getParent()->createRNG(this));
    }

    bool Modified = false;
    const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

    bool First = true;
    MachineBasicBlock &MBB = MF.front();
    const MachineBasicBlock::iterator &MBBI = MBB.begin();
    MCSymbol *JumpTarget = nullptr;
    MachineInstr *LastInstr;
    unsigned NOPs = RNG->Random(MaxPrologPaddingInstructions - MinPrologPaddingInstructions + 1) + MinPrologPaddingInstructions;
    for (unsigned I = 0; I < NOPs; I++) {
      if (First) {
        JumpTarget = MF.getContext().createLinkerPrivateTempSymbol();
        BuildMI(MBB, MBBI, DebugLoc(), TII->get(X86::JMP_4)).addSym(JumpTarget);
        First = false;
      }

      LastInstr = BuildMI(MBB, MBBI, DebugLoc(), TII->get(X86::TRAP));
    }

    if (JumpTarget) {
      assert(LastInstr && "Last TRAP instr not set");
      LastInstr->setPostInstrSymbol(MF, JumpTarget);
    }

    return Modified;
  }

  StringRef getPassName() const override { return "X86 prolog padding pass"; }

private:
  std::unique_ptr<llvm::RandomNumberGenerator> RNG;
};
char X86PrologPadding::ID = 0;
} // End anonymous namespace.

namespace llvm {
FunctionPass *createX86PrologPaddingPass() { return new X86PrologPadding(); }
} // namespace llvm