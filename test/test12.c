#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <unistd.h>
#include <malloc.h>

#include "libaflsnapshot.h"

#define ALLOCATION_SIZE 0x2000
#define ALLOCATION_CONTENT 0xcc

void print_program_break(void) {
  void *current_program_break = sbrk(0);
  if (current_program_break == (void *)-1) {
    perror("Could not get program break");
    exit(1);
  }
  printf("program break: %p\n", current_program_break);
}

int main(void) {
  if (afl_snapshot_init() == -1) {
    perror("Initialization failed");
    exit(1);
  }

  print_program_break();
  unsigned char *allocation = malloc(ALLOCATION_SIZE);
  printf("Allocated memory from the heap at: %p\n", allocation);
  print_program_break();

  allocation[ALLOCATION_SIZE - 1] = ALLOCATION_CONTENT;

  bool is_restored = false;
  if (afl_snapshot_take(AFL_SNAPSHOT_MMAP | AFL_SNAPSHOT_FDS |
                        AFL_SNAPSHOT_REGS)) {
    puts("Snapshot taken");
    is_restored = false;
  } else {
    puts("Snapshot restored");
    is_restored = true;
  }

  print_program_break();

  printf("Touching heap allocation: %p\n", &allocation[ALLOCATION_SIZE - 1]);
  if (allocation[ALLOCATION_SIZE - 1] != ALLOCATION_CONTENT) {
    printf("Content not restored: 0x%x != 0x%x\n",
           allocation[ALLOCATION_SIZE - 1], ALLOCATION_CONTENT);
    exit(1);
  }

  puts("Freeing allocated memory on the heap and trimming");
  free(allocation);
  malloc_trim(0);

  print_program_break();

  if (!is_restored) {
    puts("Restoring snapshot");
    afl_snapshot_restore();
  }

  puts("Success!");

  return 0;
}
