#include <stdio.h>
#include <stdlib.h>
#include <sys/user.h>
#include "heapbt_common.h"

extern int __heap_bt_num;
extern int *__heap_bt_array0;

static void saveBoobyTrap(int CurrentIndex, void *Pointer) {
  /*
   * The way we emit the global array in LLVM causes the address of the
   * first element to be stored in the GOT. However, accessing
   * the array in C code with '__heap_bt_array0' loads the address from the
   * GOT and automatically dereferences it. Thus, __heap_bt_array0[0] tries
   * to dereference the first array *value* instead of its address. We need
   * to take back one reference with the address-of operator.
   */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"
  int CurrentValue = (int)(&__heap_bt_array0)[CurrentIndex];
#pragma clang diagnostic pop
  if (CurrentValue != 0xAAAA) {
    printf("Unexpected value at %d\n", CurrentIndex);
    exit(EXIT_FAILURE);
  }
  (&__heap_bt_array0)[CurrentIndex] = Pointer;
}

static void __attribute__((constructor(1))) init() {
  DEBUG("Using non-hardened boobytrap array with %d elements\n", __heap_bt_num);
  setupBoobyTraps(__heap_bt_num, saveBoobyTrap);
}
