//===-- NoopInsertion.h - Noop Insertion ------------------------*- C++ -*-===//
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

#ifndef LLVM_CODEGEN_NOOPINSERTION_H
#define LLVM_CODEGEN_NOOPINSERTION_H

#include "MachineBlockFrequencyInfo.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include <random>

namespace llvm {

class RandomNumberGenerator;

class NoopInsertion : public MachineFunctionPass {
public:
  static char ID;

  NoopInsertion();

private:
  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  std::unique_ptr<llvm::RandomNumberGenerator> RNG;

  MachineBlockFrequencyInfo *BFI;
  std::unique_ptr<ProfileSummary> Summary;
};
} // namespace llvm

#endif // LLVM_CODEGEN_NOOPINSERTION_H