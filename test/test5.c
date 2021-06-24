#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>

#include "libaflsnapshot.h"

#define CHUNK_SIZE 5

int main(void) {
  if (afl_snapshot_init() == -1) {
    perror("Initialization failed");
    exit(1);
  }

  int res_file = open("./res", O_RDONLY);
  if (res_file < 0) {
    perror("Could not open resource file");
    return 1;
  }

  char chunk[CHUNK_SIZE];
  read(res_file, chunk, CHUNK_SIZE - 1);
  chunk[CHUNK_SIZE - 1] = '\0';
  printf("first chunk: \"%s\"\n", chunk);

  bool is_restored = false;
  if (afl_snapshot_take(AFL_SNAPSHOT_MMAP | AFL_SNAPSHOT_FDS |
                        AFL_SNAPSHOT_REGS)) {
    puts("Snapshot taken");
    is_restored = false;
  } else {
    puts("Snapshot restored");
    is_restored = true;
  }

  read(res_file, chunk, CHUNK_SIZE - 1);
  printf("second chunk: \"%s\"\n", chunk);

  close(res_file);

  if (!is_restored) {
    puts("Restoring snapshot");
    afl_snapshot_restore();
  }

  return 0;
}