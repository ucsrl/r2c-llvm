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
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Target/TargetMachine.h"
#include <queue>
#include <signal.h>

using namespace llvm;
using namespace r2c;

#define DEBUG_TYPE "x86-r2c-bt"
STATISTIC(NumBTRACallSites, "Number of BTRA protected call sites");

namespace {
class X86InsertBT : public ModulePass {
public:
  static char ID;
  X86InsertBT() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    AU.addPreserved<MachineModuleInfoWrapperPass>();
    AU.setPreservesAll();
    ModulePass::getAnalysisUsage(AU);
  }

  bool runOnModule(Module &M) override;

  StringRef getPassName() const override {
    return "X86 BTRA booby trap insertion pass";
  }

private:
  bool ExpandMI(Module &M, MachineBasicBlock &MBB,
                MachineBasicBlock::iterator MBBI);
  bool ExpandMBB(Module &M, MachineBasicBlock &MBB);

  MCSymbol *SetupBTTrampolines(Module &M, MachineFunction *MF,
                               size_t NumTrampolines,
                               SmallVectorImpl<GlobalValue *> &Trampolines);
  MachineOperand CreateBTMachineOperand(GlobalValue *GV);

  const X86Subtarget *STI;
  const X86InstrInfo *TII;
  const X86RegisterInfo *TRI;
  X86MachineFunctionInfo *X86FI;
  const X86FrameLowering *X86FL;
  MachineModuleInfo *MMI;
  bool UpdateCFI = false;
  size_t TrampolineCounter = 0;
  size_t ReturnLocationCounter = 0;
  std::unique_ptr<RandomNumberGenerator> RNG;
  SmallVector<GlobalValue *, 1000> ModuleTrampolines;

  void InsertPush(MachineBasicBlock *BB, MachineInstr &MI, const DebugLoc &DL,
                  unsigned OpCode, MachineOperand &Op);
  void Reset(MachineFunction &MF);
  MCSymbol *InsertVEXDecoys(Module &M, MachineBasicBlock &MBB, MachineInstr &MI,
                            const DebugLoc &DL, int Before, int After);
  MCSymbol *InsertPushDecoys(Module &M, MachineBasicBlock &MBB,
                             MachineInstr &MI, const DebugLoc &DL,
                             unsigned Before, unsigned After);
  GlobalValue *GetBoobyTrap(Module &M, MachineFunction *MF);
};
char X86InsertBT::ID = 0;
} // End anonymous namespace.

MachineOperand X86InsertBT::CreateBTMachineOperand(GlobalValue *GV) {
  // We need the trampolines to be resolved via the GOT. In particular, we need
  // a place to read the trampoline's *address* from. Other flags lead to the
  // pushes dereferencing the trampoline's address and therefore pushing the
  // first 8 bytes of the trampoline code
  return MachineOperand::CreateGA(GV, 0, X86II::MO_GOTPCREL);
}

GlobalValue *X86InsertBT::GetBoobyTrap(Module &M, MachineFunction *MF) {
  FunctionCallee ReportCallee = M.getOrInsertFunction(
      "ReportAttack",
      FunctionType::get(Type::getVoidTy(M.getContext()), false));
  Function *ReportTarget = dyn_cast<Function>(ReportCallee.getCallee());
  if (MaxBoobyTrapTrampolines == 0 ||
      this->ModuleTrampolines.size() < MaxBoobyTrapTrampolines) {
    FunctionType *FT =
        FunctionType::get(FunctionType::getVoidTy(M.getContext()), false);
    std::string BTName =
        ("BTRA_BT_" + MF->getName() + Twine(TrampolineCounter++).str()).str();
    FunctionCallee TrampolineCallee = M.getOrInsertFunction(BTName, FT);
    Function *Trampoline = dyn_cast<Function>(TrampolineCallee.getCallee());
    assert(Trampoline != nullptr && "Created function null");

    Trampoline->setLinkage(GlobalValue::PrivateLinkage);
    Trampoline->addFnAttr(Attribute::NonLazyBind);
    Trampoline->addFnAttr(Attribute::OptimizeForSize);
    BasicBlock *EntryBB =
        BasicBlock::Create(M.getContext(), "entry", Trampoline);
    IRBuilder<> Builder(EntryBB);
    Builder.CreateRetVoid();
    MachineFunction &TrampolineMF =
        MMI->getOrCreateMachineFunction(*Trampoline);
    TrampolineMF.getProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
    TrampolineMF.getRegInfo().freezeReservedRegs(TrampolineMF);
    MachineBasicBlock *NewBlock = TrampolineMF.CreateMachineBasicBlock();
    TrampolineMF.insert(TrampolineMF.begin(), NewBlock);
    BuildMI(NewBlock, DebugLoc(), TII->get(X86::JMP_4))
        .addGlobalAddress(ReportTarget);
    ModuleTrampolines.push_back(Trampoline);
    return Trampoline;
  } else {
    unsigned Index = RNG->Random(this->ModuleTrampolines.size());
    return this->ModuleTrampolines[Index];
  }
}

MCSymbol *
X86InsertBT::SetupBTTrampolines(Module &M, MachineFunction *MF,
                                size_t NumTrampolines,
                                SmallVectorImpl<GlobalValue *> &Trampolines) {
  //  GlobalValue *BT = M.getFunction("BoobyTrap");
  //  if (!BT) {
  //    BT = CreateBoobyTrap(M, MF);
  //  }

  MCSymbol *RL = MF->getContext().createLinkerPrivateTempSymbol();
  //  MCSymbol *RL = MF->getContext().getOrCreateSymbol(
  //      MF->getName() + "ReturnLocation" +
  //      Twine(ReturnLocationCounter++).str());
  for (size_t I = 0; I < NumTrampolines; I++) {
    Trampolines.push_back(GetBoobyTrap(M, MF));
  }
  return RL;
}

void X86InsertBT::InsertPush(MachineBasicBlock *BB, MachineInstr &MI,
                             const DebugLoc &DL, unsigned OpCode,
                             MachineOperand &Op) {
  auto RelocModel = BB->getParent()->getTarget().getRelocationModel();
  MachineInstrBuilder Builder = BuildMI(*BB, MI, DL, TII->get(OpCode));
  if (RelocModel == llvm::Reloc::PIC_) {
    Builder.addReg(X86::RIP)
        .addImm(1)
        .addReg(0)
        .addGlobalAddress(Op.getGlobal(), 0, Op.getTargetFlags())
        .addReg(0);
  } else {
    Builder.addGlobalAddress(Op.getGlobal());
  }

  if (UpdateCFI) {
    X86FL->BuildCFI(
        *BB, MI, DL,
        MCCFIInstruction::createAdjustCfaOffset(nullptr, X86FL->SlotSize));
  }
}

static Type *getPtrTy(const Module &M) {
  auto Bits = M.getDataLayout().getPointerSizeInBits();
  return Type::getIntNTy(M.getContext(), Bits); // Pointers are stored as ints
}

MCSymbol *X86InsertBT::InsertPushDecoys(Module &M, MachineBasicBlock &MBB,
                                        MachineInstr &MI, const DebugLoc &DL,
                                        unsigned Before, unsigned After) {
  SmallVector<GlobalValue *, 10> Trampolines;
  MCSymbol *ReturnLocation =
      SetupBTTrampolines(M, MBB.getParent(), Before + After, Trampolines);
  auto RelocModel = MBB.getParent()->getTarget().getRelocationModel();
  unsigned PushOpCode =
      RelocModel == Reloc::PIC_ ? X86::PUSH64rmm : X86::PUSH64i32;

  for (unsigned I = 0; I < Trampolines.size(); I++) {
    if (I == Before && After > 0) {
      BuildMI(MBB, MI, DL, TII->get(PushOpCode)).addSym(ReturnLocation);
      if (UpdateCFI) {
        X86FL->BuildCFI(
            MBB, MI, DL,
            MCCFIInstruction::createAdjustCfaOffset(nullptr, X86FL->SlotSize));
      }
    }
    MachineOperand Op = CreateBTMachineOperand(Trampolines[I]);
    InsertPush(&MBB, MI, DL, PushOpCode, Op);
    if (RNG->Random(100) <= 35) {
      BuildMI(MBB, MI, DL, TII->get(X86::NOOP));
    }
  }

  return ReturnLocation;
}

template <typename T>
static void swapPositions(SmallVectorImpl<T> &Elements, int i, int j) {
  T Tmp = Elements[i];
  Elements[i] = Elements[j];
  Elements[j] = Tmp;
}

MCSymbol *X86InsertBT::InsertVEXDecoys(Module &M, MachineBasicBlock &MBB,
                                       MachineInstr &MI, const DebugLoc &DL,
                                       int Before, int After) {
  SmallVector<GlobalValue *, 10> Trampolines;
  MCSymbol *ReturnLocation =
      SetupBTTrampolines(M, MBB.getParent(), Before + After, Trampolines);

  auto *PtrTy = getPtrTy(M);

  SmallVector<Constant *, 10> TrampolinePtrs;
  for (int I = 0; I < Before; I++) {
    auto *FnPtr = ConstantExpr::getPtrToInt(Trampolines[I], PtrTy);
    TrampolinePtrs.push_back(FnPtr);
  }

  TrampolinePtrs.push_back(ConstantExpr::getNullValue(PtrTy));

  for (int I = 0; I < After; I++) {
    auto *FnPtr = ConstantExpr::getPtrToInt(Trampolines[Before + I], PtrTy);
    TrampolinePtrs.push_back(FnPtr);
  }

  bool HasAVX512 = STI->hasAVX512();
  unsigned Multiple = HasAVX512 ? 8 : 4;

  while (TrampolinePtrs.size() % Multiple != 0) {
    TrampolinePtrs.push_back(
        ConstantExpr::getPtrToInt(GetBoobyTrap(M, MBB.getParent()), PtrTy));
  }

  int Count = TrampolinePtrs.size();

  for (size_t Pos = 0; Pos + 3 < TrampolinePtrs.size(); Pos += 4) {
    swapPositions(TrampolinePtrs, Pos, Pos + 3);
    swapPositions(TrampolinePtrs, Pos + 1, Pos + 2);
  }

  // Create an array containing the trampoline pointers
  ArrayType *AT = ArrayType::get(PtrTy, TrampolinePtrs.size());
  std::string ArrayName(
      (MBB.getParent()->getName() + "_bts" + Twine(ReturnLocationCounter++))
          .str());
  M.getOrInsertGlobal(ArrayName, AT);
  GlobalVariable *Array = M.getNamedGlobal(ArrayName);
  Array->setAlignment(MaybeAlign(HasAVX512 ? 64 : 32));
  Array->setLinkage(GlobalValue::PrivateLinkage);
  Array->setInitializer(ConstantArray::get(AT, TrampolinePtrs));
  MachineOperand Op = MachineOperand::CreateGA(Array, 0, 0);

  MBB.getParent()->getContext().addBoobyTrapReturnLoc(Array->getName(),
                                                      ReturnLocation);

  int NumMovs = Count / Multiple;
  int MaxNops = 3;
  Register Reg = HasAVX512 ? X86::ZMM13 : X86::YMM13;
  for (int I = 0; I < NumMovs; I++) {
    for (MCRegAliasIterator AReg(Reg, TRI, true); AReg.isValid(); ++AReg) {
      assert(!MBB.isLiveIn(*AReg));
    }
    BuildMI(MBB, MI, DL,
            TII->get(HasAVX512 ? X86::VMOVDQA64Zrm : X86::VMOVDQAYrm), Reg)
        .addReg(X86::RIP)
        .addImm(1)
        .addReg(0)
        .addGlobalAddress(Op.getGlobal(), I * Multiple * X86FL->SlotSize,
                          Op.getTargetFlags())
        .addReg(0);

    int NumNops = RNG->Random(MaxNops);
    for (int J = 0; J < NumNops; J++) {
      BuildMI(MBB, MI, DL, TII->get(X86::NOOP));
    }

    addRegOffset(
        BuildMI(MBB, MI, DL,
                TII->get(HasAVX512 ? X86::VMOVDQU64Zmr : X86::VMOVDQUYmr)),
        X86::RSP, true, -((I + 1) * Multiple * X86FL->SlotSize))
        .addReg(Reg);
  }

  // Register Reg = HasAVX512 ? X86::ZMM13 : X86::YMM13;
  // for (int I = 0; I < NumMovs; I++) {
  //   for (MCRegAliasIterator AReg(Reg, TRI, true); AReg.isValid(); ++AReg) {
  //     assert(!MBB.isLiveIn(*AReg));
  //   }
  //   BuildMI(MBB, MI, DL,
  //           TII->get(HasAVX512 ? X86::VMOVDQA64Zrm : X86::VMOVDQAYrm), Reg)
  //       .addReg(X86::RIP)
  //       .addImm(1)
  //       .addReg(0)
  //       .addGlobalAddress(Op.getGlobal(), I * Multiple * X86FL->SlotSize,
  //                         Op.getTargetFlags())
  //       .addReg(0);
  //   addRegOffset(
  //       BuildMI(MBB, MI, DL,
  //               TII->get(HasAVX512 ? X86::VMOVDQU64Zmr : X86::VMOVDQUYmr)),
  //       X86::RSP, true, -((I + 1) * Multiple * X86FL->SlotSize))
  //       .addReg(Reg);
  // }
  BuildMI(MBB, MI, DL, TII->get(X86::VZEROUPPER));

  //
  //  Register Regs[] = {X86::XMM6, X86::XMM7, X86::XMM8};
  //  errs() << "XMM #Trampolines: " << Count << "\n";
  //  int Factor = 2;
  //  assert(Count % Factor == 0 && "The total number of decoys must be even");
  //  int NumMovs = Count / Factor;
  //  for (int I = 1; I <= NumMovs; I++) {
  //    if (MBB.isLiveIn(Regs[I]))
  //      continue;
  //
  //    BuildMI(MBB, MI, DL, TII->get(X86::VMOVDQArm), Regs[I % 3])
  //        .addReg(X86::RIP)
  //        .addImm(1)
  //        .addReg(0)
  //        .addGlobalAddress(Op.getGlobal(), 2 * X86FL->SlotSize,
  //        Op.getTargetFlags()) .addReg(0);
  //
  //    addRegOffset(BuildMI(MBB, MI, DL, TII->get(X86::VMOVDQAmr)), X86::RSP,
  //                 true, -(I * Factor * X86FL->SlotSize))
  //        .addReg(Regs[I % 3]);
  //  }

  return ReturnLocation;
}

bool X86InsertBT::ExpandMI(Module &M, MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned Opcode = MI.getOpcode();
  DebugLoc DL = MBBI->getDebugLoc();

  if (Opcode != X86::INSBTRAS) {
    return false;
  }

  int64_t Before = MI.getOperand(0).getImm();
  int64_t After = MI.getOperand(1).getImm();
  assert((Before >= 0 && After >= 0) && "Negative BTRA count");

  MCSymbol *ReturnLocation;
  if (r2c::UseVEXInstructions) {
    ReturnLocation = InsertVEXDecoys(M, MBB, MI, DL, Before, After);
  } else {
    ReturnLocation = InsertPushDecoys(M, MBB, MI, DL, Before, After);
  }

  MBBI++;
  MI.eraseFromParent();
  while (MBBI != MBB.end() && !MBBI->isCall())
    MBBI++;

  assert(MBBI != MBB.end() && "Call instruction not found");
  MBBI->setPostInstrSymbol(*MBB.getParent(), ReturnLocation);

  return true;
}

bool X86InsertBT::ExpandMBB(Module &M, MachineBasicBlock &MBB) {
  bool Modified = false;

  // MBBI may be invalidated by the expansion.
  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= ExpandMI(M, MBB, MBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool X86InsertBT::runOnModule(Module &M) {
  if (Implementation.hasFlag(DisableDiversity)) {
    return false;
  }

  MMI = &getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
  if (!RNG)
    RNG.reset(M.createRNG(this));

  bool Modified = false;
  std::queue<Function *> ModuleWorkList;
  for (auto &F : M) {
    if (!F.isDeclaration()) {
      ModuleWorkList.push(&F);
    }
  }

  while (!ModuleWorkList.empty()) {
    Function *F = ModuleWorkList.front();
    ModuleWorkList.pop();
    MachineFunction &MF = *MMI->getMachineFunction(*F);
    Reset(MF);
    for (MachineBasicBlock &MBB : MF)
      Modified |= ExpandMBB(M, MBB);
  }

  return Modified;
}
void X86InsertBT::Reset(MachineFunction &MF) {
  STI = &static_cast<const X86Subtarget &>(MF.getSubtarget());
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();
  X86FL = STI->getFrameLowering();
  X86FI = MF.getInfo<X86MachineFunctionInfo>();
  UpdateCFI = !X86FL->hasFP(MF) || X86FI->getCallerPreparedFP();
  TrampolineCounter = 0;
  ReturnLocationCounter = 0;
}

namespace {
class X86BTRAUnfusion : public MachineFunctionPass {
public:
  static char ID;
  X86BTRAUnfusion() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    AU.addPreserved<MachineModuleInfoWrapperPass>();
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    bool Modified = false;
    for (MachineBasicBlock &MBB : MF) {
      // MBBI may be invalidated by the expansion.
      MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
      while (MBBI != E) {
        MachineBasicBlock::iterator NMBBI = std::next(MBBI);

        if (MBBI->isBundle()) {
          MachineBasicBlock::instr_iterator MII = MBBI.getInstrIterator();
          MII++;
          if (MII->getOpcode() == X86::INSBTRAS) {
            MachineBasicBlock::instr_iterator II(MBBI->getIterator());
            for (MachineBasicBlock::instr_iterator I = ++II,
                                                   E = MBB.instr_end();
                 I != E && I->isBundledWithPred(); ++I) {
              I->unbundleFromPred();
              for (MachineOperand &MO : I->operands())
                if (MO.isReg())
                  MO.setIsInternalRead(false);
            }

            MBBI->eraseFromParent();
          }
        }

        MBBI = NMBBI;
      }
    }

    return Modified;
  }

  StringRef getPassName() const override { return "X86 BTRA unfusion pass"; }
};
char X86BTRAUnfusion::ID = 0;
} // End anonymous namespace.

namespace {
class X86BTRAFusion : public MachineFunctionPass {
public:
  static char ID;
  X86BTRAFusion() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    AU.addPreserved<MachineModuleInfoWrapperPass>();
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    bool Modified = false;
    for (MachineBasicBlock &MBB : MF) {
      // MBBI may be invalidated by the expansion.
      MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
      while (MBBI != E) {
        MachineBasicBlock::iterator NMBBI = std::next(MBBI);

        // Fuse INSBTRAS instructions with the following call instruction into
        // a bundle to prevent separation
        if (MBBI->getOpcode() == X86::INSBTRAS) {
          MachineBasicBlock::instr_iterator FirstMI = MBBI.getInstrIterator();
          MachineBasicBlock::instr_iterator LastMI = FirstMI;
          while (!(LastMI->isCall()))
            LastMI++;
          ++LastMI;
          finalizeBundle(MBB, FirstMI, LastMI);
          Modified = true;
        }

        MBBI = NMBBI;
      }
    }

    return Modified;
  }

  StringRef getPassName() const override { return "X86 BTRA Fusion pass"; }
};
char X86BTRAFusion::ID = 0;
} // End anonymous namespace.

FunctionPass *llvm::createX86BTRAFusionPass() { return new X86BTRAFusion(); }
FunctionPass *llvm::createX86BTRAUnfusionPass() { return new X86BTRAUnfusion(); }

/// Returns an instance of the pseudo instruction expansion pass.
ModulePass *llvm::createX86InsertBTPass() { return new X86InsertBT(); }
