#include "llvm/R2COptions/R2COptions.h"

using namespace llvm;

namespace r2c {

cl::opt<unsigned int> MaxBoobyTrapTrampolines(
    "x86-max-booby-trap-trampolines",
    cl::desc("The maximum number of booby trap trampolines which provide the "
             "booby trap addresses"),
    cl::init(0), cl::Hidden);

struct ImplementationVariants Implementation;

cl::opt<bool>
    EnableStatistics("enable-statistics",
                  cl::desc("Collect statistics for R2COptions implementations"),
                  cl::Hidden);

cl::opt<bool>
    ShuffleFunctions("shuffle-functions",
                     cl::desc("Shuffle the functions in the LTO module"),
                     cl::Hidden);

cl::opt<bool>
    ShuffleGlobals("shuffle-globals",
                     cl::desc("Shuffle global variables"),
                     cl::Hidden);


cl::opt<unsigned int> GlobalMinCount("global-min-count",
                   cl::desc("Minimum number of globals"),
                   cl::Hidden);

llvm::cl::opt<unsigned int>
    GlobalPaddingPercentage("global-padding-percentage",
                            llvm::cl::desc("Percentage of globals that get random padding"),
                            llvm::cl::init(0));

llvm::cl::opt<unsigned int>
    GlobalPaddingMaxSize("global-padding-max-size",
                         llvm::cl::desc("Maximum size of random padding between globals, in bytes"),
                         llvm::cl::init(64));


cl::opt<bool> RandomizeAfterSpace("randomize-after-space",
    cl::desc("Randomly choose the space reserved for after decoys in the prologue"),
    cl::init(true), cl::Hidden);

cl::opt<bool> UseVEXInstructions("use-vex-instructions",
                                  cl::desc("Use VEX instructions to optimize decoy setup"),
                                  cl::init(true), cl::Hidden);

cl::opt<unsigned int> NumBtras(
    "num-btras",
    cl::desc("The maximum number of decoys to use"),
    cl::init(0), cl::Hidden);

cl::opt<bool> ExportBTRASection("export-btra-section",
                                  cl::desc("Add a custom section with BTRA metadata to the resulting binary"),
                                  cl::init(false), cl::Hidden);

cl::opt<R2CCallee> AssumeR2CCallee(
    "assume-btra-callee",
    cl::desc("Assume that unknown callees are R2COptions compiled"),
    cl::values(
        clEnumValN(NoR2C, "no", "Assume that the callee is not R2C compiled"),
        clEnumValN(Maybe, "maybe", "Assume that the callee could be R2C compiled, but do not require it"),
        clEnumValN(Force, "force", "Expect that the calllee is R2C compiled")),
    cl::init(NoR2C), cl::Hidden);

cl::opt<bool> HardenHeapBoobyTraps(
    "harden-heap-boobytraps",
    cl::desc("Use hardened heap boobytraps"),
    cl::init(true),
    cl::Hidden);

cl::opt<unsigned int> HeapPtrArraySize(
    "heap-ptr-array-size",
    cl::desc("The number of heap pointer booby traps to initialize"),
    cl::init(300),
    cl::Hidden);

cl::opt<unsigned int> MaxHeapPtrBoobyTraps(
    "max-heap-ptr-boobytraps",
    cl::desc("The number of heap pointer dummies to push onto the stack"),
    cl::init(0),
    cl::Hidden);

cl::opt<unsigned int> MaxPtrArrayBoobyTraps(
    "max-ptr-array-boobytraps",
    cl::desc("The number of heap pointer dummies with which to disguise the heap booby trap array pointer"),
    cl::Hidden);


cl::opt<bool> ShuffleStackObjects(
    "shuffle-stack-objects",
    cl::desc("Shuffle the non-fixed objects on the stack"),
    cl::Hidden);

cl::opt<bool> RandomizeRegAlloc(
    "randomize-reg-alloc",
    cl::desc("Randomize register allocation"),
    cl::Hidden);

cl::opt<ImplementationVariants, true, cl::parser<unsigned int>>
    DiversityMode("diversity-mode",
                                              cl::location(Implementation),
                                              cl::init(0), cl::Hidden);

// See LibSupportInfoOutputFilename in Timer.cpp for an explanation of this
// weird construct
static ManagedStatic<std::string> RadCallSiteStatistiicsFileName;
static std::string &getRadCallSiteStatisticsFileName() {
  return *RadCallSiteStatistiicsFileName;
}

cl::opt<std::string, true>
    CallSiteStatisticsFileName("call-site-range-stats-file", cl::value_desc("filename"),
                       cl::desc("File to append R2C call site parameter range statistics output to"),
                       cl::Hidden, cl::location(getRadCallSiteStatisticsFileName()));

// legacy options

cl::opt<unsigned int> MaxNumOfFunctionVariants(
    "x86-max-function-variants",
    cl::desc("The maximum number of variants to generate for each function"),
    cl::init(5), cl::Hidden);

cl::opt<unsigned int>
    MaxNumberOfVTablePointers("max-number-of-vptrs",
                              cl::desc("Maximum number of vptrs"), cl::init(1));

} // namespace r2c