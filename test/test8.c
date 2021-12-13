#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>

#include "libaflsnapshot.h"

#define MAPPING_SIZE 0x100000000000UL

int main(void) {
  int *map_ptr = mmap(NULL, MAPPING_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_NORESERVE | MAP_ANON, -1, 0);
  printf("Mapped %zu TB of memory\n", MAPPING_SIZE / 1024 / 1024 / 1024 / 1024);

  puts("The value of map_ptr should be restored.");

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

  map_ptr[0] += 1;

  printf("Running, map_ptr: %p = %d\n", map_ptr, map_ptr[0]);

  if (!is_restored) {
    puts("Restoring snapshot");
    afl_snapshot_restore();
  }

  if (map_ptr[0] != 1) {
    puts("Failure!");
    exit(1);
  }
  puts("Success!");

  return 0;
}
