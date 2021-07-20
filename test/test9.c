#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <unistd.h>

#include "libaflsnapshot.h"

int main(void) {
  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size == -1) {
    perror("Could not retrieve page size");
    exit(1);
  }

  unsigned char *map_ptr =
      mmap(NULL, page_size, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);

  if (afl_snapshot_init() == -1) {
    perror("Initialization failed");
    exit(1);
  }

  afl_snapshot_include_vmrange(map_ptr, map_ptr + page_size);

  bool is_restored = false;
  if (afl_snapshot_take(AFL_SNAPSHOT_MMAP | AFL_SNAPSHOT_FDS |
                        AFL_SNAPSHOT_REGS)) {
    puts("Snapshot taken");
    is_restored = false;
  } else {
    puts("Snapshot restored");
    is_restored = true;
  }

  if (mprotect(map_ptr, page_size, PROT_READ | PROT_WRITE) == -1) {
    perror("Could not make memory writable");
    exit(1);
  }

  map_ptr[0] += 1;

  if (mprotect(map_ptr, page_size, PROT_READ) == -1) {
    perror("Could not reprotect memory");
    exit(1);
  }

  printf("Running, map_ptr: %p = %d\n", map_ptr, map_ptr[0]);

  if (!is_restored) {
    puts("Restoring snapshot");
    afl_snapshot_restore();
  }

  if (map_ptr[0] != 1) { return 1; }

  return 0;
}
