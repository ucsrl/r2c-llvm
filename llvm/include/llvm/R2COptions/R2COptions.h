//
// Created by felixl on 27.12.2018.
//

#ifndef LLVM_R2COptions_H
#define LLVM_R2COptions_H

#include "llvm/Support/CommandLine.h"

namespace r2c {
struct NumOpt {
  NumOpt() : Min(_min), Max(_max) {}

  struct Num {
    Num(unsigned int &parentValue) : _parentValue(parentValue) {}

    Num &operator=(const Num &other) {
      _parentValue = other._parentValue;
      return *this;
    }

    void operator=(const int n) { withValue(n); }

    Num &withValue(unsigned int n) {
      _parentValue = n;
      return *this;
    }

  private:
    unsigned int &_parentValue;
  };

  Num Min;
  Num Max;

  unsigned int min() { return _min; }
  unsigned int max() { return std::max(_min, _max); }

private:
  unsigned int _min = 0;
  unsigned int _max = 0;
};

extern llvm::cl::opt<unsigned int> HeapPtrArraySize;
extern llvm::cl::opt<unsigned int> MaxHeapPtrBoobyTraps;
extern llvm::cl::opt<unsigned int> MaxPtrArrayBoobyTraps;
extern llvm::cl::opt<unsigned int> NumBtras;
extern llvm::cl::opt<bool> ShuffleStackObjects;

enum R2CCallee { NoR2C, Maybe, Force
};

extern llvm::cl::opt<bool> EnableStatistics;
extern llvm::cl::opt<bool> ShuffleFunctions;
extern llvm::cl::opt<bool> ShuffleGlobals;
extern llvm::cl::opt<unsigned int> GlobalMinCount;
extern llvm::cl::opt<unsigned int> GlobalPaddingPercentage;
extern llvm::cl::opt<unsigned int> GlobalPaddingMaxSize;
extern llvm::cl::opt<bool> HardenHeapBoobyTraps;
extern llvm::cl::opt<bool> RandomizeRegAlloc;
extern llvm::cl::opt<bool> RandomizeAfterSpace;
extern llvm::cl::opt<bool> UseVEXInstructions;
extern llvm::cl::opt<bool> ExportBTRASection;
extern llvm::cl::opt<R2CCallee> AssumeR2CCallee;
extern llvm::cl::opt<unsigned int> MaxBoobyTrapTrampolines;
extern llvm::cl::opt<unsigned int> MaxNumOfFunctionVariants;
extern llvm::cl::opt<NumOpt::Num, true, llvm::cl::parser<unsigned int>>
    ColdMaxAfter;
extern llvm::cl::opt<NumOpt::Num, true, llvm::cl::parser<unsigned int>>
    ColdMinAfter;
extern llvm::cl::opt<NumOpt::Num, true, llvm::cl::parser<unsigned int>>
    ColdMaxBefore;
extern llvm::cl::opt<NumOpt::Num, true, llvm::cl::parser<unsigned int>>
    ColdMinBefore;
extern llvm::cl::opt<std::string, true>
    CallSiteStatisticsFileName;

enum ImplementationFlags {
  DisableDiversity = 1,
  UsePushes = 2,
  Immediate8Bit = 4,
  Immediate16Bit = 8,
  UseBoobyTraps = 16,
  UseMovs = 32,
  UseNops = 64,
  NoCallerFPSetup = 128,
  NoCallerBTSetup = 256,
  EpilogueBTs = 512,
  AlwaysUseBTs = 1024,
  NoPGO = 2048,
};

struct ImplementationVariants {
  ImplementationVariants() {}

  ImplementationVariants(unsigned int steps) { setImplementationFlags(steps); }

  const ImplementationVariants &operator=(const int steps) {
    setImplementationFlags(steps);
    return *this;
  }

  bool hasFlag(ImplementationFlags flag) { return Implementation & flag; }

private:
  unsigned Implementation;

  void setImplementationFlags(int steps) {
    Implementation = 0;
    switch (steps) {
    case 0:
      Implementation |= DisableDiversity;
      break;
    case 1:
      Implementation |= UsePushes;
      Implementation |= Immediate8Bit;
      break;
    case 2:
      Implementation |= UsePushes;
      Implementation |= Immediate16Bit;
      break;
    case 3:
      Implementation |= UsePushes;
      Implementation |= UseBoobyTraps;
      break;
    case 4:
      Implementation |= UseMovs;
      Implementation |= UseBoobyTraps;
      break;
    case 5:
      Implementation |= UseNops;
      break;
    case 6:
      Implementation |= UseNops;
      Implementation |= NoCallerFPSetup;
      break;
    case 7:
      Implementation |= UsePushes;
      Implementation |= EpilogueBTs;
      Implementation |= NoCallerBTSetup;
      break;
    case 8:
      Implementation |= NoCallerBTSetup;
      break;
    case 9:
      Implementation |= UsePushes;
      Implementation |= EpilogueBTs;
      Implementation |= UseBoobyTraps;
      break;
    case 10:
      Implementation |= UsePushes;
      Implementation |= UseBoobyTraps;
      Implementation |= NoPGO;
      break;
    }
  }
};

extern ImplementationVariants Implementation;

extern llvm::cl::opt<ImplementationVariants, true,
                     llvm::cl::parser<unsigned int>>
    DiversityMode;

} // namespace r2c

#endif // LLVM_R2COptions_H
