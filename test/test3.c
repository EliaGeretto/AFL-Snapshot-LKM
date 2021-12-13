#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <assert.h>

#include "libaflsnapshot.h"

static void run_as_child(void) {
  bool is_restored = false;
  if (afl_snapshot_take(AFL_SNAPSHOT_MMAP | AFL_SNAPSHOT_FDS |
                        AFL_SNAPSHOT_REGS)) {
    puts("Snapshot taken");
    is_restored = false;
  } else {
    puts("Snapshot restored");
    is_restored = true;
  }

  puts("Running");

  if (!is_restored) {
    puts("Restoring snapshot");
    afl_snapshot_restore();
  }
}

int main(void) {
  if (afl_snapshot_init() == -1) {
    perror("Initialization failed");
    exit(1);
  }

  puts("Forking child process");

  pid_t pid = fork();
  if (pid == -1) {
    perror("Could not fork child process");
    exit(1);
  } else if (pid == 0) {
    run_as_child();
    exit(0);
  } else {
    int   status = 0;
    pid_t waited_pid = wait(&status);
    assert(pid == waited_pid);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      puts("Child exited correctly");
    } else {
      puts("Child did not exit correctly");
    }
  }

  puts("\"Running\" should appear twice");

  return 0;
}
