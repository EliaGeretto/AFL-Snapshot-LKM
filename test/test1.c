#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>

#include "afl_snapshot.h"
#include "libaflsnapshot.h"

bool test(uint8_t *shm_addr, uint8_t *none_addr, size_t page_size) {
  *shm_addr = 0;
  *none_addr = 0;

  fprintf(stderr, "shm_addr: %p, none_addr: %p\n", shm_addr, none_addr);

  if (afl_snapshot_take(AFL_SNAPSHOT_NOSTACK) == 1) { puts("Snapshot taken"); }

  for (size_t idx = 0; idx < 10; idx++) {
    *none_addr += 1;
    *(none_addr + page_size) += 1;
    *shm_addr += 1;
    *(shm_addr + page_size) += 1;

    fprintf(stderr, "shm_addr[0]: %d, none_addr[0]: %d\n", *shm_addr,
            *none_addr);
    fprintf(stderr, "shm_addr[1]: %d, none_addr[1]: %d\n",
            *(shm_addr + page_size), *(none_addr + page_size));

    puts("Restoring snapshot");
    afl_snapshot_restore();
  }

  if (*shm_addr != 0) { return false; }

  if (*(shm_addr + page_size) != 10) { return false; }

  if (*none_addr != 10) { return false; }

  if (*(none_addr + page_size) != 0) { return false; }

  return true;
}

int main() {
  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size == -1) {
    perror("Could not retrieve page size");
    exit(1);
  }

  if (afl_snapshot_init() == -1) {
    perror("AFL snapshot initialization failed");
    exit(1);
  }

  uint8_t *shm_addr = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (shm_addr == MAP_FAILED) {
    perror("Could not map shared memory");
    exit(1);
  }

  uint8_t *none_addr = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (none_addr == MAP_FAILED) {
    perror("Could not map private memory");
    exit(1);
  }

  afl_snapshot_exclude_vmrange(none_addr, none_addr + page_size);
  afl_snapshot_include_vmrange(shm_addr, shm_addr + page_size);

  if (!test(shm_addr, none_addr, page_size)) { exit(1); }

  return 0;
}
