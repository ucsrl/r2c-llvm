#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Pass.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/R2C.h"
#include <llvm/Analysis/ProfileSummaryInfo.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/InitializePasses.h>
#include <llvm/R2COptions/R2COptions.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <queue>
#include <signal.h>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace llvm;
using namespace r2c;

#define DEBUG_TYPE "diversity-pass"

STATISTIC(NumBoobyTrapTrampolines, "Number of booby trap trampolines inserted");
STATISTIC(AssumedBTRACallSite, "Number of assumed BTRA call sites");
STATISTIC(NumBTRACallees, "Number of BTRA call sites");
STATISTIC(NumNonBTRACallees, "Number of call sites whose callee is not BTRA compiled");

namespace {
static ManagedStatic<sys::SmartMutex<true> > RangeStatisticsLock;

struct RangeStatisticsTracker {
  bool Enabled = false;
  RangeStatisticsTracker() {
    const std::string &OutputFilename = r2c::CallSiteStatisticsFileName;
    Enabled = !OutputFilename.empty();
  }

  ~RangeStatisticsTracker() {
    if (!Enabled) return;
    std::unique_ptr<raw_ostream> OutStream = CreateInfoOutputFile();
    // Print all of the statistics.
    *OutStream << "{\n";
    *OutStream << "\t\"Before\": {\n";
    PrintRangeMap(BeforeRangeMap, *OutStream);
    *OutStream << "},\n";
    *OutStream << "\t\"After\": {\n";
    PrintRangeMap(AfterRangeMap, *OutStream);
    *OutStream << "},\n";
    *OutStream << "\t\"Counts\": {\n";
    PrintCountMap(CountMap, *OutStream);
    *OutStream << "}\n";
    *OutStream << "\n}\n";
    OutStream->flush();
  }

  std::unique_ptr<raw_fd_ostream> CreateInfoOutputFile() {
    const std::string &OutputFilename = r2c::CallSiteStatisticsFileName;

    // Append mode is used because the info output file is opened and closed
    // each time -stats or -time-passes wants to print output to it. To
    // compensate for this, the test-suite Makefiles have code to delete the
    // info output file before running commands which write to it.
    std::error_code EC;
    auto Result = std::make_unique<raw_fd_ostream>(
        OutputFilename, EC, sys::fs::OF_Append | sys::fs::OF_Text);
    if (!EC)
      return Result;

    errs() << "Error opening call site statistics file '"
           << OutputFilename << "' for appending!\n";
    return std::make_unique<raw_fd_ostream>(2, false); // stderr.
  }


  typedef std::pair<unsigned, unsigned> range;
  typedef std::map<range, std::map<unsigned, unsigned>> range_distribution_map;
  typedef std::map<unsigned, std::map<std::pair<range, range>, unsigned>> count_distribution_map;

  void RecordRange(bool Before, range R, unsigned Count) {
    if (!Enabled) return;

    sys::SmartScopedLock<true> Reader(*RangeStatisticsLock);
    if (Before) {
      BeforeRangeMap[R][Count]++;
    } else {
      AfterRangeMap[R][Count]++;
    }
  }

  void RecordTotalCount(unsigned TotalCount, range Before, range After) {
    if (!Enabled) return;

    sys::SmartScopedLock<true> Reader(*RangeStatisticsLock);
    CountMap[TotalCount][std::make_pair(Before, After)]++;
  }

private:
  void PrintRangeMap(range_distribution_map M, raw_ostream &OS) {
    const char *Delim = "";
    for (auto Entry: M) {
      OS << Delim;
      OS << "\t\t\"";
      PrintRange(Entry.first, OS);
      OS << "\": [\n";
      const char *DelimInner = "";
      for (auto DistEntry : Entry.second) {
        OS << DelimInner;
        OS << "\t\t\t{ \"" << DistEntry.first << "\": " << DistEntry.second << " }";
        DelimInner = ",\n";
      }
      OS << "\n\t\t]";
      Delim = ",\n";
    }
  }

  void PrintRange(range R, raw_ostream &OS) {
    OS << "L" << R.first << "U" << R.second ;
  }

  void PrintCountMap(count_distribution_map M, raw_ostream &OS) {
    const char *Delim = "";
    for (auto Entry: M) {
      OS << Delim;
      OS << "\t\t\"" << Entry.first << "\": [\n";
      const char *DelimInner = "";
      for (auto DistEntry : Entry.second) {
        OS << DelimInner;
        OS << "\t\t\t{ \"";
        PrintRange(DistEntry.first.first, OS);
        OS << ",";
        PrintRange(DistEntry.first.second, OS);
        OS << "\": " << DistEntry.second << " }";
        DelimInner = ",\n";
      }
      OS << "\n\t\t]";
      Delim = ",\n";
    }
  }

  range_distribution_map BeforeRangeMap;
  range_distribution_map AfterRangeMap;
  count_distribution_map CountMap;
};

struct DiversityPass : public ModulePass {
  static char ID; // Pass identification, replacement for typeid

  const ModuleSummaryIndex *ImportSummary;

    DiversityPass() : ModulePass(ID) {
        initializeDiversityPassPass(*PassRegistry::getPassRegistry());
    }

    DiversityPass(const ModuleSummaryIndex *Summary) : ModulePass(ID), ImportSummary(Summary) {
    initializeDiversityPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &Usage) const override;

  bool runOnModule(Module &M) override {
    bool Changed = false;

    if (Implementation.hasFlag(DisableDiversity)) {
      return false;
    }

    if (!RNG)
      RNG.reset(M.createRNG(this));
    PSI = &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
    LLVM_DEBUG(dbgs() << "Running on module " << M.getName() << "\n");
    LLVM_DEBUG(dbgs() << "Profile info: " << (PSI->hasProfileSummary() ? "yes" : "no") << "\n");


    std::queue<Function *> ModuleWorkList;
    for (auto &F : M) {
      if (!F.isDeclaration()) {
        ModuleWorkList.push(&F);
      }
    }

    LLVM_DEBUG(dbgs() << "Found " << ModuleWorkList.size() << " functions to process" << "\n");

    LLVMContext &Context = M.getContext();
    MDBuilder MDB(Context);
    std::vector<Metadata *> BTRAMetadataNodes;
    while (!ModuleWorkList.empty()) {
      Function *F = ModuleWorkList.front();
      ModuleWorkList.pop();

      if (isBTRAFunction(F, M)) {
        F->addFnAttr("diversity-btra-candidate", llvm::toStringRef(true));
        unsigned int ReserveSpaceFor = RandomizeAfterSpace ? RNG->Random(r2c::NumBtras + 1) : r2c::NumBtras;
        F->addFnAttr("diversity-btra-space-for", llvm::utostr(ReserveSpaceFor));
        if (F->hasExternalLinkage() && r2c::ExportBTRASection) {
          Metadata *Entry[] = {
              ValueAsMetadata::get(F),
              MDB.createConstant(ConstantInt::get(Type::getInt32Ty(Context),
                                                  ReserveSpaceFor))};
          BTRAMetadataNodes.push_back(MDNode::get(Context, Entry));
        }
        LLVM_DEBUG(dbgs() << "Function " << F->getName() << " is a BTRA candidate" << "\n");
      }

      if (usePGO()) {
        BFI = &getAnalysis<BlockFrequencyInfoWrapperPass>(*F).getBFI();
        Summary.reset(ProfileSummary::getFromMD(M.getProfileSummary(false)));
      }

      for (auto &Block : *F){
        for (auto &Instruction : Block){
          if (auto *CS = dyn_cast<CallBase>(&Instruction)) {
            AttrBuilder Builder;
            unsigned NumBeforeDecoys;
            unsigned NumAfterDecoys;

            bool RadCallSite = false;

            if (CS->getCalledFunction() == nullptr) {
              Builder.addAttribute("diversity-maybe-btra-callee");
              RadCallSite = true;
            } else {
              if (isBTRAFunction(CS->getCalledFunction(), M) ||
                  AssumeR2CCallee == Force) {
                  Builder.addAttribute("diversity-btra-callee");
                  RadCallSite = true;
                  LLVM_DEBUG(dbgs() << "Callee " << CS->getCalledFunction()->getName() << " is a known BTRA candidate" << "\n");
              } else if (AssumeR2CCallee == Maybe) {
                bool SupportedCallee = true;
                if (F->isDeclaration() && F->getIntrinsicID()) {
                  unsigned IID = F->getIntrinsicID();
                  switch (IID) {
                  case Intrinsic::memcpy:
                  case Intrinsic::memcpy_inline:
                  case Intrinsic::memset:
                  case Intrinsic::memmove:
                    SupportedCallee = true;
                    break;
                  default:
                    SupportedCallee = false;
                    LLVM_DEBUG(
                        dbgs()
                            << "Callee " << CS->getCalledFunction()->getName()
                            << " is an unsupported intrinsicc\n");
                  }
                }

                if (SupportedCallee) {
                  Builder.addAttribute("diversity-maybe-btra-callee");
                  RadCallSite = true;
                  AssumedBTRACallSite++;
                  LLVM_DEBUG(
                      dbgs()
                      << "Callee " << CS->getCalledFunction()->getName()
                      << " is not a known BTRA candidate, but we assume it"
                      << "\n");
                }
              } else {
                NumNonBTRACallees++;
              }
            }

            if (RadCallSite) {
              NumBTRACallees++;
              getDecoyCountForCallSite(CS, NumBeforeDecoys, NumAfterDecoys);

              Builder.addAttribute("diversity-btra-before", llvm::utostr(NumBeforeDecoys));
              Builder.addAttribute("diversity-btra-after", llvm::utostr(NumAfterDecoys));
              Builder.addAttribute("diversity-btra-call", llvm::utostr(true));

              bool CallerBTSetup = !(r2c::Implementation.hasFlag(r2c::NoCallerBTSetup));

              if (usePGO() &&
                  r2c::Implementation.hasFlag(r2c::EpilogueBTs) &&
                      PSI->isHotCallSite(*CS, BFI)) {
                  CallerBTSetup = false;
              }

              if (CallerBTSetup) {
                Builder.addAttribute("diversity-btra-bt-setup", llvm::utostr(true));
              } else {
                Builder.addAttribute("diversity-btra-bt-setup", llvm::utostr(false));
              }

              AttributeList PAL = CS->getAttributes().addAttributes(
                  CS->getContext(), AttributeList::FunctionIndex, Builder);
              CS->setAttributes(PAL);

              Changed = true;
            }
          }
        }
      }
    }

    if (!BTRAMetadataNodes.empty()) {
      M.addModuleFlag(Module::Append, "BTRA Info", MDNode::get(Context, BTRAMetadataNodes));
    }

    return Changed;
  }


private:
  bool usePGO() {
    return !Implementation.hasFlag(NoPGO) && PSI->hasProfileSummary();
  }

  bool isBTRAFunction(Function *F, Module &M) {

    if (ImportSummary) {
      ValueInfo V = ImportSummary->getValueInfo(F->getGUID());
      if (V && !V.getSummaryList().empty()) {
        GlobalValueSummary *S = V.getSummaryList().front()->getBaseObject();
        FunctionSummary *FS = cast<FunctionSummary>(S);
        FunctionSummary::FFlags Flags = FS->fflags();
        return Flags.BTRA;
      }
    }

    // TODO: investigate when exactly the function is not found in the index
    // I assmume it has something to do with ThinLTO Symbol Linkage and Renaming
    // Apparently the linkage type of the functions not found in the index was
    // changed by function import. However, the functions (including their
    // metadata) are available in the module itself
    return F->hasFnAttribute("diversity-btra-candidate");
  }

  void getDecoyCountForCallSite(CallBase *CS, unsigned &NumBeforeDecoys,
                                unsigned &NumAfterDecoys) {

    NumBeforeDecoys = getNumberInRange(0, r2c::NumBtras);
    NumAfterDecoys = NumBtras - NumBeforeDecoys;

    // if available, limit the number of after decoys to the number reserved
    // by the callee
    Function *Fn = CS->getCalledFunction();
    if (Fn != nullptr) {
      if (Fn->hasFnAttribute("diversity-btra-space-for")) {
        unsigned FunctionAfterDecoys;
        Fn->getFnAttribute("diversity-btra-space-for")
            .getValueAsString()
            .getAsInteger(10, FunctionAfterDecoys);
        ;
        NumAfterDecoys = std::min(NumAfterDecoys, FunctionAfterDecoys);
      }
    }
  }

  static unsigned scaleToRange(unsigned long X, unsigned long Max, unsigned RMin, unsigned RMax) {
    return round(RMax - (RMax - RMin) * (log10(1 + X) / log10(1 + Max)));
  }

  std::pair<unsigned, unsigned> getRange(unsigned Count, unsigned MaxCount, unsigned LLower, unsigned LUpper, unsigned ULower, unsigned UUpper) {
    unsigned ScaledL = scaleToRange(Count, MaxCount, LLower, LUpper);
    unsigned ScaledU = scaleToRange(Count, MaxCount, ULower, UUpper);

    return std::make_pair(ScaledL, ScaledU);
  }

  static void markAsUsed(GlobalValue *GV, Module &M) {
    SmallVector<llvm::GlobalValue *, 1> Used;
    Used.push_back(GV);
    appendToUsed(M, GV);
  }

  unsigned int getNumberInRange(unsigned int Min, unsigned int Max) const {
      unsigned int CombinedMax = std::max(Min, Max);
      if (CombinedMax > Min) {
        return RNG->Random(CombinedMax - Min + 1) + Min;
      }
      return Min;
    }

    // RNG instance for this pass
  std::unique_ptr<RandomNumberGenerator> RNG;
  BlockFrequencyInfo *BFI;
  ProfileSummaryInfo *PSI;
  std::vector<Function *> BoobyTrapTrampolines;
  std::unique_ptr<ProfileSummary> Summary;

};
} // namespace

namespace llvm {
    ModulePass *createDiversityPass(const ModuleSummaryIndex *ImportSummary) {
        return new DiversityPass(ImportSummary);
    }
} // namespace llvm

char DiversityPass::ID = 0;
void DiversityPass::getAnalysisUsage(AnalysisUsage &Usage) const {
  Usage.addRequired<BlockFrequencyInfoWrapperPass>();
  Usage.addRequired<ProfileSummaryInfoWrapperPass>();
  Usage.getPreservesAll();
}


INITIALIZE_PASS_BEGIN(DiversityPass, "diversitypass", "BTRA calculations", false,
                      false)
  INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(BlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_END(DiversityPass, "diversitypass", "BTRA calculations", false,
                    false)
