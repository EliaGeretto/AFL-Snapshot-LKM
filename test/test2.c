#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "libaflsnapshot.h"

static int global_var = 0;

static bool test() {
  if (afl_snapshot_take(AFL_SNAPSHOT_NOSTACK) == 1) {
    fputs("Snapshot taken\n", stderr);
  }

  for (size_t idx = 0; idx < 10; idx++) {
    global_var++;
    fprintf(stderr, "global_var: %d\n", global_var);
    afl_snapshot_restore();
  }

  if (global_var != 0) { return false; }

  return true;
}

int main(void) {
  if (afl_snapshot_init() == -1) {
    perror("AFL snapshot initialization failed");
    exit(1);
  }

  fputs("global_var should be restored.\n", stderr);

  if (!test()) {
    fputs("Failure!\n", stderr);
    exit(1);
  }
  fputs("Success!\n", stderr);

  return 0;
}
