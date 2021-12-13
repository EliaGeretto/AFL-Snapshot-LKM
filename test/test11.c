#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <unistd.h>

#include "libaflsnapshot.h"

#define MAP_CONTENT 0x42

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

  unsigned char *map_addr = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANON, -1, 0);
  if (map_addr == MAP_FAILED) {
    perror("Could not map memory");
    exit(1);
  }

  printf("Mapped memory at: %p\n", map_addr);

  map_addr[0] = MAP_CONTENT;

  bool is_restored = false;
  if (afl_snapshot_take(AFL_SNAPSHOT_MMAP | AFL_SNAPSHOT_FDS |
                        AFL_SNAPSHOT_REGS)) {
    puts("Snapshot taken");
    is_restored = false;
  } else {
    puts("Snapshot restored");
    is_restored = true;
  }

  printf("Touching memory at: %p\n", map_addr);
  if (map_addr[0] != MAP_CONTENT) {
    printf("Content not restored: 0x%x != 0x%x\n",
           map_addr[0], MAP_CONTENT);
    exit(1);
  }

  printf("Unmapping memory at: %p\n", map_addr);
  if (munmap(map_addr, page_size) == -1) {
    perror("Could not unmap memory");
    exit(1);
  }

  if (!is_restored) {
    puts("Restoring snapshot");
    afl_snapshot_restore();
  }

  puts("Success!");

  return 0;
}
