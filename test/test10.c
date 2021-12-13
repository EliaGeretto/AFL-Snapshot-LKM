#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <unistd.h>

#include "libaflsnapshot.h"

#define MAP_ADDR (void *)0x10000

int main(void) {
  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size == -1) {
    perror("Could not retrieve page size");
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

  unsigned char *map_addr =
      mmap(MAP_ADDR, page_size, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANON | MAP_FIXED_NOREPLACE, -1, 0);
  if (map_addr == MAP_FAILED) {
    printf("Could not map at address: %p", MAP_ADDR);
    exit(1);
  }

  printf("Successful mapping at address: %p\n", map_addr);

  map_addr[0] = 0x42;

  if (!is_restored) {
    puts("Restoring snapshot");
    afl_snapshot_restore();
  }

  printf("Both mappings at %p should succeed.\n", map_addr);

  return 0;
}
