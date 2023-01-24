//===-- X86MachineFunctionInfo.cpp - X86 machine function info ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "X86MachineFunctionInfo.h"
#include "X86RegisterInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include <llvm/R2COptions/R2COptions.h>

using namespace llvm;

void X86MachineFunctionInfo::anchor() { }

void X86MachineFunctionInfo::setRestoreBasePointer(const MachineFunction *MF) {
  if (!RestoreBasePointerOffset) {
    const X86RegisterInfo *RegInfo = static_cast<const X86RegisterInfo *>(
      MF->getSubtarget().getRegisterInfo());
    unsigned SlotSize = RegInfo->getSlotSize();
    for (const MCPhysReg *CSR = MF->getRegInfo().getCalleeSavedRegs();
         unsigned Reg = *CSR; ++CSR) {
      if (X86::GR64RegClass.contains(Reg) || X86::GR32RegClass.contains(Reg))
        RestoreBasePointerOffset -= SlotSize;
    }
  }
}

unsigned
X86MachineFunctionInfo::getNumAfterDecoys(const MachineFunction *MF) const {
  const Function &Fn = MF->getFunction();
  unsigned int NumAfterDecoys = 0;
  if (!r2c::Implementation.hasFlag(r2c::ImplementationFlags::DisableDiversity)) {
    size_t ReserveSpace = 0;
    if (Fn.hasFnAttribute("diversity-btra-space-for")) {
      Fn.getAttributes()
          .getFnAttributes()
          .getAttribute("diversity-btra-space-for")
          .getValueAsString()
          .getAsInteger(10, ReserveSpace);
    }
    NumAfterDecoys = ReserveSpace;
  }

  return NumAfterDecoys;
}
