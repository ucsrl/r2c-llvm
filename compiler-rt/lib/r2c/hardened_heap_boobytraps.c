#pragma clang diagnostic push
#pragma ide diagnostic ignored "readability-identifier-naming"
#include <stdio.h>
#include <stdlib.h>
#include <sys/user.h>
#include "heapbt_common.h"

extern int __heap_bt_num;
extern long **__heap_bt_array_ptr;

extern int __heap_bt_array_bt_num;
extern long *__heap_bt_array_bt_array;


static void saveBoobyTrap(int CurrentIndex, void *Pointer) {
    __heap_bt_array_ptr[CurrentIndex] = Pointer;
}

static void initializeArrayBoobyTraps() {
  DEBUG("Initializing %d array booby trap pointers\n", __heap_bt_array_bt_num);

  for (int I = 0; I < __heap_bt_array_bt_num; I++) {
    long *CurrentValue = (long *)(&__heap_bt_array_bt_array)[I];
    (&__heap_bt_array_bt_array)[I] = 0;
    if (*CurrentValue != 0xBBBB) {
      printf("Unexpected booby trap pointer value at %d: %ld\n", I, *CurrentValue);
      exit(EXIT_FAILURE);
    }
    *CurrentValue = (long) __heap_bt_array_ptr[__heap_bt_num + I];
  }
}

static void __attribute__((constructor(1))) init() {
  unsigned TotalAmount = __heap_bt_num + __heap_bt_array_bt_num;
  DEBUG("Using hardened boobytrap array with %d elements\n", TotalAmount);
  __heap_bt_array_ptr = malloc(TotalAmount * sizeof (void *));
  setupBoobyTraps(TotalAmount, saveBoobyTrap);
  initializeArrayBoobyTraps();
}

#pragma clang diagnostic pop
