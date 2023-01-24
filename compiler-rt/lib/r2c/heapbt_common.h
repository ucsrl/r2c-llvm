//
// Created by felixl on 06/05/22.
//

#ifndef LLVM_HEAPBT_COMMON_H
#define LLVM_HEAPBT_COMMON_H
#define ERROR(msg)                                                             \
  do {                                                                         \
    perror(msg);                                                               \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

#define DEBUG(msg, ...)                                                        \
  do {                                                                         \
    if (getenv("HEAP_BOOBYTRAP_DEBUG") != NULL) {                                                        \
      fprintf(stderr, "[HEAP_BT] " msg, __VA_ARGS__);                                   \
    }                                                                          \
  } while (0)

typedef void (*InstallBoobyTrap)(int, void *);
void setupBoobyTraps(int NumBoobyTraps,
                     InstallBoobyTrap callback);
#endif // LLVM_HEAPBT_COMMON_H
