#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "libaflsnapshot.h"

#define CHUNK_SIZE 5

int main(void) {
  if (afl_snapshot_init() == -1) {
    perror("Initialization failed");
    exit(1);
  }

  FILE *res_file = fopen("./res", "r");
  if (!res_file) {
    perror("Could not open resource file");
    return 1;
  }

  char chunk[CHUNK_SIZE];
  fgets(chunk, CHUNK_SIZE, res_file);
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

  fgets(chunk, CHUNK_SIZE, res_file);
  printf("second chunk: \"%s\"\n", chunk);

  fclose(res_file);

  if (!is_restored) {
    puts("Restoring snapshot");
    afl_snapshot_restore();
  }

  printf("The second chunk should be \" is \" both times.\n");

  return 0;
}
