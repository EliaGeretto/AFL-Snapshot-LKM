#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "libaflsnapshot.h"

void *sleep_task(void *p) {
  puts("secondary thread sleeping for 3 seconds");
  sleep(3);
  return NULL;
}

int main(void) {
  if (afl_snapshot_init() == -1) {
    perror("Initialization failed");
    exit(1);
  }

  puts("Spawning secondary thread");
  pthread_t secondary_thread;
  pthread_create(&secondary_thread, NULL, sleep_task, NULL);

  bool is_restored = false;
  if (afl_snapshot_take(AFL_SNAPSHOT_MMAP | AFL_SNAPSHOT_FDS |
                        AFL_SNAPSHOT_REGS)) {
    puts("Snapshot taken");
    is_restored = false;
  } else {
    puts("Snapshot restored");
    is_restored = true;
  }

  puts("Running main thread");

  if (!is_restored) {
    puts("Restoring snapshot");
    afl_snapshot_restore();
  }

  pthread_join(secondary_thread, NULL);

  return 0;
}
