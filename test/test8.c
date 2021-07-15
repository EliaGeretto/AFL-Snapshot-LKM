#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>

#include "libaflsnapshot.h"

#define MAPPING_SIZE 0x100000000000UL

int main(void) {
  void *mapping = mmap(NULL, MAPPING_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_NORESERVE | MAP_ANON, -1, 0);
  printf("Mapped %zu TB of memory\n", MAPPING_SIZE / 1024 / 1024 / 1024 / 1024);

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

  return 0;
}
