//
// Created by felixl on 28/04/22.
//

#include <stdio.h>
#include <stdlib.h>

__attribute__((unused))
void ReportAttack() {
  printf("A booby trap function was hit\n");
  abort();
}
