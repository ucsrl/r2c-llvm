#include "heapbt_common.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <time.h>

static long parseNumber(const char *String) {
  long Result = strtol(String, NULL, 10);
  if (errno != 0 && Result == 0) {
    perror("strtol");
  }
  return Result;
}

struct FreeList {
  struct FreeList *Prev;
};

static void *getBoobyTrapPointer(int UseConstant, const void *Alloc) {
  void *Pointer = !UseConstant
                      ? ((int *)Alloc + ((rand() * sizeof(int)) % PAGE_SIZE))
                      : ((int *)0xABCDEF);
  DEBUG("PT_POINTER: %p\n", Pointer);
  return Pointer;
}

void *getBoobyTrapPointer(int UseConstant, const void *Alloc);
// TODO: use proper RNG
void setupBoobyTraps(int NumBoobyTraps, InstallBoobyTrap InstallCallback) {
  int CurrentIndex = 0;
  int UseConstant = getenv("HEAP_BOOBYTRAP_CONSTANT") != NULL;
  int NoSetup = getenv("HEAP_BOOBYTRAP_NOSETUP") != NULL;
  char *BoobyTrapNewPageProbEnv = getenv("HEAP_BOOBYTRAP_PROB_NEW_PAGE");
  char *BoobyTrapGapPageProbEnv = getenv("HEAP_BOOBYTRAP_PROB_GAP");
  char *SeedEnv = getenv("HEAP_BOOBYTRAP_SEED");
  long NewPageProb = 30;
  long GapProb = 30;
  time_t TimeStruct;
  long Seed = time(&TimeStruct);

  if (BoobyTrapNewPageProbEnv != NULL) {
    NewPageProb = parseNumber(BoobyTrapNewPageProbEnv);
  }

  if (BoobyTrapGapPageProbEnv != NULL) {
    GapProb = parseNumber(BoobyTrapGapPageProbEnv);
  }
  DEBUG("NewPageProb: %ld\n", NewPageProb);
  DEBUG("GapProb: %ld\n", GapProb);

  if (SeedEnv != NULL) {
    Seed = parseNumber(SeedEnv);
  }

  if (NoSetup) {
    DEBUG("WARNING: Using fixed pointers for debugging\n", NULL);
    while (CurrentIndex < NumBoobyTraps) {
      InstallCallback(CurrentIndex, ((int *)0xABCDEF));
      CurrentIndex++;
    }
  } else {
    srand(Seed);
    // void **Array = malloc(NumBoobyTraps * sizeof (void *));
    struct FreeList *Current = NULL;
    void *LastAlloc = NULL;
    while (CurrentIndex < NumBoobyTraps) {
      if ((rand() % 101) <= NewPageProb || CurrentIndex == 0) {
        void *Alloc = aligned_alloc(PAGE_SIZE + 1, PAGE_SIZE);
        if ((rand() % 101) <= GapProb) {
          struct FreeList *Entry = (struct FreeList *)Alloc;
          if (Current == NULL) {
            Current = Entry;
            Entry->Prev = NULL;
          } else {
            Entry->Prev = Current;
            Current = Entry;
          }
          DEBUG("GAP: %p\n", Alloc, PAGE_SIZE);
        } else {
          if (mprotect(Alloc, 1, PROT_NONE)) {
            ERROR("mmap");
          }
          DEBUG("GUARD: %p\n", Alloc);
          void *Pointer = getBoobyTrapPointer(UseConstant, Alloc);
          InstallCallback(CurrentIndex, Pointer);
          CurrentIndex++;
          LastAlloc = Alloc;
        }
      } else {
        void *Pointer = getBoobyTrapPointer(UseConstant, LastAlloc);
        InstallCallback(CurrentIndex, Pointer);
        CurrentIndex++;
        DEBUG("REUSE: %p\n", LastAlloc, PAGE_SIZE);
      }
    }

    while (Current != NULL) {
      void *Free = Current;
      Current = Current->Prev;
      DEBUG("Freeing hole %p\n", Free);
      free(Free);
    }
  }
}
