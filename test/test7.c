#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/resource.h>

#include "libaflsnapshot.h"

int main(void) {
  struct rlimit core_lim = {
      .rlim_cur = 0,
      .rlim_max = 0,
  };
  if (setrlimit(RLIMIT_CORE, &core_lim) == -1) {
    perror("Could not set core limit");
    exit(1);
  }

  if (afl_snapshot_init() == -1) {
    perror("Initialization failed");
    exit(1);
  }

  bool is_restored = false;
  if (afl_snapshot_take(AFL_SNAPSHOT_MMAP | AFL_SNAPSHOT_FDS |
                        AFL_SNAPSHOT_REGS)) {
    puts("Snapshot taken");
    is_restored = false;
  } else {
    puts("Snapshot restored");
    is_restored = true;
  }

  puts("Running");

  if (!is_restored) {
    puts("Restoring snapshot");
    afl_snapshot_restore();
  }

  puts("The program should abort without kernel errors.");

  abort();

  return 0;
}
