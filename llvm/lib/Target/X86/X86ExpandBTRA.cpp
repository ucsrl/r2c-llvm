//
// Created by felixl on 6/9/20.
//

#include "X86.h"
#include "X86FrameLowering.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h" // For IDs of passes that are preserved.
#include "llvm/IR/GlobalValue.h"
#include "llvm/R2COptions/R2COptions.h"
#include <queue>
#include <signal.h>

using namespace llvm;
using namespace r2c;

#define DEBUG_TYPE "x86-btra"
STATISTIC(NumBTRACallSites, "Number of BTRA protected call sites");

namespace {
class X86ExpandBTRA : public MachineFunctionPass {
public:
  static char ID;
  X86ExpandBTRA() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    AU.addPreserved<MachineModuleInfoWrapperPass>();
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }


  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "X86 BTRA instruction expansion pass";
  }

private:
  void EmitBTRASetup(MachineBasicBlock *MBB, MachineBasicBlock::iterator MBBI);
  void EmitBTRATeardown(MachineBasicBlock *MBB,
                       MachineBasicBlock::iterator MBBI);

  bool ExpandMI(MachineBasicBlock &MBB,
                MachineBasicBlock::iterator MBBI);
  bool ExpandMBB(MachineBasicBlock &MBB);

  MachineInstr *BTRASetupInst = nullptr;
  MachineInstr *AdjCallStackInst = nullptr;
  MachineInstr *BTRAAfterInsertBefore = nullptr;
  size_t SetupDecoys(MachineBasicBlock *BB, const DebugLoc &DL,
                         size_t Before, size_t After, MachineInstr &MI,
                         SmallVectorImpl<GlobalValue *> &Trampolines);
  size_t SetupSPAdjustment(MachineBasicBlock *BB, const DebugLoc &DL,
                           size_t Before, MachineInstr &MI) const;

  size_t BeforeSpace = 0;
  const X86Subtarget *STI;
  const X86InstrInfo *TII;
  const X86RegisterInfo *TRI;
  X86MachineFunctionInfo *X86FI;
  const X86FrameLowering *X86FL;
  bool UpdateCFI = false;
};
char X86ExpandBTRA::ID = 0;
} // End anonymous namespace.

bool X86ExpandBTRA::ExpandMI(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned Opcode = MI.getOpcode();
  DebugLoc DL = MBBI->getDebugLoc();

  // If we see a CallFrameSetup instruction (ADJCALLSTACKDOWN), save it so
  // we can later adjust frame index references if necessary.
  if (Opcode == TII->getCallFrameSetupOpcode()) {
    AdjCallStackInst = &MI;
    return false;
  }

  // Clean up the pointer to the CallFrameSetup instruction when we see the
  // second half of the pair.
  if (Opcode == TII->getCallFrameDestroyOpcode()) {
    AdjCallStackInst = nullptr;
    return false;
  }

  if (Opcode == X86::BTRASETUP || Opcode == X86::BTRASETUPWITHFP) {
    BTRASetupInst = &MI;
    return false;
  }

  if (MI.isCall() && BTRASetupInst) {
    BTRAAfterInsertBefore = &*(std::next(MBBI));
    EmitBTRASetup(&MBB, MBBI);
    return true;
  }

  if (Opcode == X86::BTRATEARDOWN || Opcode == X86::BTRATEARDOWNWITHFP) {
    EmitBTRATeardown(&MBB, MBBI);
    return true;
  }
  
  return false;
}

bool X86ExpandBTRA::ExpandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  // MBBI may be invalidated by the expansion.
  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= ExpandMI(MBB, MBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool X86ExpandBTRA::runOnMachineFunction(MachineFunction &MF){
  if (Implementation.hasFlag(DisableDiversity)) {
    return false;
  }
  MachineFunction &mf = MF;
  STI = &static_cast<const X86Subtarget &>(mf.getSubtarget());
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();
  X86FL = STI->getFrameLowering();
  X86FI = mf.getInfo<X86MachineFunctionInfo>();
  UpdateCFI = !X86FL->hasFP(mf) || X86FI->getCallerPreparedFP();
  X86FI->setDecoyOffset(&mf, X86FI->getNumAfterDecoys(&mf) * X86FL->SlotSize);

  bool Modified = false;
  for (MachineBasicBlock &MBB : MF)
    Modified |= ExpandMBB(MBB);


  return Modified;
}

void X86ExpandBTRA::EmitBTRASetup(MachineBasicBlock *BB,
                                MachineBasicBlock::iterator MBBI) {

  assert(BTRASetupInst && "BTRA setup insertion without setup instruction");
  MachineInstr &Call = *MBBI;
  assert(Call.isCall() && "Current instruction is not a call instruction");
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MBBI->getDebugLoc();
  MachineFunction *MF = BB->getParent();
  MachineFrameInfo MFI = MF->getFrameInfo();

  assert(BTRASetupInst->getOperand(0).getImm() >= 0 && "Negative before decoy count in BTRASetup instruction");
  assert(BTRASetupInst->getOperand(1).getImm() >= 0 && "Negative after decoy count in BTRASetup instruction");

  size_t Before = BTRASetupInst->getOperand(0).getImm();
  size_t After = BTRASetupInst->getOperand(1).getImm();

  bool CallerSavedFP = BTRASetupInst->getOpcode() == X86::BTRASETUPWITHFP;

  X86MachineFunctionInfo *FuncInfo =
      BB->getParent()->getInfo<X86MachineFunctionInfo>();

  if (CallerSavedFP || Before > 0 || After > 0) {
    FuncInfo->setHasBTRACalls(true);
  }

  Register FramePtr = TRI->getFramePtr();
  Register StackPtr = TRI->getStackRegister();

  // If we need to setup a frame pointer for the callee, spill the current
  // frame pointer register
  if (CallerSavedFP) {
    assert(FuncInfo->getCallerFPSpillSlotFI().hasValue() &&
           "Caller must prepare FP, but no FP spill slot");
    addFrameReference(BuildMI(*BB, MI, DL, TII->get(X86::MOV64mr)),
                      FuncInfo->getCallerFPSpillSlotFI().getValue())
        .addReg(FramePtr);
    BuildMI(*BB, MI, DL, TII->get(X86::MOV64rr), FramePtr).addReg(StackPtr);
  }

  if (Implementation.hasFlag(ImplementationFlags::NoCallerBTSetup)) {
    BeforeSpace = SetupSPAdjustment(BB, DL, Before, MI);
  } else {
    SmallVector<GlobalValue *, 10> BTTrampolines;
    if (Implementation.hasFlag(ImplementationFlags::UsePushes)) {
      BeforeSpace = SetupDecoys(BB, DL, Before, After, MI, BTTrampolines);
    }
  }

  NumBTRACallSites++;

  assert(AdjCallStackInst && "No ADJCALLSTACKDOWN instruction before BTRA setup");
  auto InsertionPos = std::next(MachineBasicBlock::instr_iterator(AdjCallStackInst));
  BuildMI(*BB, InsertionPos, DL, TII->get(X86::ADJARGSIZE)).addImm(BeforeSpace);

  BB->erase_instr(BTRASetupInst);
  BTRASetupInst = nullptr;
}

size_t X86ExpandBTRA::SetupSPAdjustment(MachineBasicBlock *BB,
                                       const DebugLoc &DL, size_t Before,
                                       MachineInstr &MI) const {
  size_t NumBytes = Before * X86FL->SlotSize;
  if (NumBytes % X86FL->getStackAlignment() != 0) {
    NumBytes += X86FL->SlotSize;
  }

  if (NumBytes > 0) {
    BuildMI(*BB, MI, DL, TII->get(X86::SUB64ri8), TRI->getStackRegister())
        .addReg(TRI->getStackRegister())
        .addImm(NumBytes);

    if (UpdateCFI) {
      X86FL->BuildCFI(
          *BB, MI, DL,
          MCCFIInstruction::createAdjustCfaOffset(nullptr, NumBytes));
    }
  }
  return NumBytes;
}


size_t X86ExpandBTRA::SetupDecoys(MachineBasicBlock *BB, const DebugLoc &DL,
                                     size_t Before, size_t After,
                                     MachineInstr &MI,
                                     SmallVectorImpl<GlobalValue *> &Trampolines) {

//  if (BB->getParent()->getName() == "_ZN11xercesc_2_715XMLTransServiceC2Ev")
//    raise(SIGTRAP);

  size_t BytesToAfterDecoys = Before * X86FL->SlotSize;
//  if (BB->getParent()->getName() == "_ZN13cConfigOptionC1EPKcbbNS_4TypeES1_S1_S1_") raise(SIGTRAP);

  // The stack needs to be aligned when executing the call instruction
  while (BytesToAfterDecoys % X86FL->getStackAlignment() != 0) {
    // TODO: randomize between adding to before or adding to after and at the same time decreasing before
    BytesToAfterDecoys += X86FL->SlotSize;
    Before++;
  }

  if ((Before + After) == 0) {
    return 0;
  }

  Register SP = TRI->getStackRegister();


  if (r2c::UseVEXInstructions) {
    BuildMI(*BB, MI, DL, TII->get(X86::INSBTRAS)).addImm(Before).addImm(After).addImm(0);

    if (BytesToAfterDecoys > 0) {
      // After the VEX instructions have set up the decoys below the stack
      // pointer, skip the before decoys and position the SP before the
      // return address slot
      BuildMI(*BB, MI, DL, TII->get(X86::SUB64ri32), SP)
          .addReg(SP)
          .addImm(BytesToAfterDecoys);

      if (UpdateCFI) {
        X86FL->BuildCFI(*BB, MI, DL,
                        MCCFIInstruction::createAdjustCfaOffset(
                            nullptr, BytesToAfterDecoys));
      }
    }
  } else {
    int SPAdj = (Before + After) * X86FL->SlotSize;
    if (After > 0) {
      SPAdj += X86FL->SlotSize;
    }
    BuildMI(*BB, MI, DL, TII->get(X86::INSBTRAS)).addImm(Before).addImm(After).addImm(SPAdj);
      if (After > 0) {
        // After the pushes, skip the after decoys and the return address slot
        size_t BytesToBeforeDecoys = X86FL->SlotSize * After + X86FL->SlotSize;
        BuildMI(*BB, MI, DL, TII->get(X86::ADD64ri32), SP)
            .addReg(SP)
            .addImm(BytesToBeforeDecoys);

        if (UpdateCFI) {
          X86FL->BuildCFI(*BB, MI, DL,
                          MCCFIInstruction::createAdjustCfaOffset(
                              nullptr, -BytesToBeforeDecoys));
        }
      }
  }


  return BytesToAfterDecoys;
}

void X86ExpandBTRA::EmitBTRATeardown(MachineBasicBlock *BB,
                                   MachineBasicBlock::iterator MBBI) {
  assert(BTRAAfterInsertBefore && "BTRA Teardown without call instruction");
  MachineInstr &MI = *BTRAAfterInsertBefore;
  MachineInstr &BTRAInstr = *MBBI;

  DebugLoc DL = MI.getDebugLoc();
  bool CallerSavedFP = BTRAInstr.getOpcode() == X86::BTRATEARDOWNWITHFP;
  Register SP = TRI->getStackRegister();
  MachineFunction *MF = BB->getParent();
  MachineFrameInfo MFI = MF->getFrameInfo();
  X86MachineFunctionInfo *FuncInfo = MF->getInfo<X86MachineFunctionInfo>();

//  if (BB->getParent()->getName() == "quantum_get_state")
//    raise(SIGTRAP);

  if (!(Implementation.hasFlag(ImplementationFlags::NoCallerFPSetup))) {
    if (BeforeSpace > 0) {
      BuildMI(*BB, MI, DL, TII->get(X86::ADD64ri32), SP)
          .addReg(SP)
          .addImm(BeforeSpace);

      if (UpdateCFI) {
        X86FL->BuildCFI(
            *BB, MI, DL,
            MCCFIInstruction::createAdjustCfaOffset(nullptr, -BeforeSpace));
      }
    }

    if (CallerSavedFP) {
      assert(FuncInfo->getCallerFPSpillSlotFI().hasValue() &&
             "Caller must prepare FP, but no FP spill slot");
      Register FramePtr = TRI->getFramePtr();
      MachineInstrBuilder MIB =
          BuildMI(*BB, MI, DL, TII->get(X86::MOV64rm)).addReg(FramePtr);
      addFrameReference(MIB, FuncInfo->getCallerFPSpillSlotFI().getValue());
    }
  }

  MBBI++;
  BTRAInstr.eraseFromParent();
}

/// Returns an instance of the pseudo instruction expansion pass.
FunctionPass *llvm::createX86ExpandBTRAPass() { return new X86ExpandBTRA(); }