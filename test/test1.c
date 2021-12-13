#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>

#include "libaflsnapshot.h"

static bool test(uint8_t *shm_addr, uint8_t *priv_addr, size_t page_size) {
  *shm_addr = 0;
  *priv_addr = 0;

  fprintf(stderr, "shm_addr: %p, priv_addr: %p\n", shm_addr, priv_addr);

  if (afl_snapshot_take(AFL_SNAPSHOT_NOSTACK) == 1) {
    fputs("Snapshot taken\n", stderr);
  }

  for (size_t idx = 0; idx < 10; idx++) {
    *priv_addr += 1;
    *(priv_addr + page_size) += 1;
    *shm_addr += 1;
    *(shm_addr + page_size) += 1;

    fprintf(stderr, "shm_addr[0]: %d, priv_addr[0]: %d\n", *shm_addr,
            *priv_addr);
    fprintf(stderr, "shm_addr[1]: %d, priv_addr[1]: %d\n",
            *(shm_addr + page_size), *(priv_addr + page_size));

    fputs("Restoring snapshot\n", stderr);
    afl_snapshot_restore();
  }

  if (*shm_addr != 0) { return false; }

  if (*(shm_addr + page_size) != 10) { return false; }

  if (*priv_addr != 10) { return false; }

  if (*(priv_addr + page_size) != 0) { return false; }

  return true;
}

int main(void) {
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

  uint8_t *priv_addr = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (priv_addr == MAP_FAILED) {
    perror("Could not map private memory");
    exit(1);
  }

  afl_snapshot_exclude_vmrange(priv_addr, priv_addr + page_size);
  afl_snapshot_include_vmrange(shm_addr, shm_addr + page_size);

  // priv_addr[0] is excluded manually
  // priv_addr[1] is included by default
  // shm_addr[0] is included manually
  // shm_addr[1] is excluded by default

  fputs("Only shm_addr[0] and priv_addr[1] should be restored.\n", stderr);

  if (!test(shm_addr, priv_addr, page_size)) {
    fputs("Failure!\n", stderr);
    exit(1);
  }
  fputs("Success!\n", stderr);

  return 0;
}
