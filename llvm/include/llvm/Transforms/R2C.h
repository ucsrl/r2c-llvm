//
// Created by felixl on 12/10/2020.
//

#ifndef LLVM_R2C_H
#define LLVM_R2C_H

namespace llvm {
    class ModulePass;
    class ModuleSummaryIndex;

    ModulePass *createR2CMarkerPass();
    ModulePass *createDiversityPass(const ModuleSummaryIndex *Summary);
    ModulePass *createHeapBoobyTrapPass();
} // namespace llvm

#endif // LLVM_R2C_H
