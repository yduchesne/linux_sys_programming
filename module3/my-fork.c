#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LOOPS 10
#define SLEEP_INTERVAL 5

/**
 * A simple program illustrating for/wait functionality for basic
 * multi-processing and process coordination:
 * 
 * - Parent forks a child process and waits on child completion.
 * - Child does some work and exits.
 * - Parent recuperates child's exit code, returns its own exit
 *   code accordingly.
 * 
 */

int main() {

  printf("Forking child process (parent pid = %u)\n", getpid());

  int childPid = fork();

  if (childPid < 0) {
    return EXIT_SUCCESS;
  }

  uint8_t loopCount = 0;

  switch (childPid) {
  // If childPid is 0: it means execution is currently
  // occurring in the child process.
  case 0:
    while (loopCount < MAX_LOOPS) {
      printf("Running child process (pid: %u, loopCount = %u)\n", getpid(),
             loopCount);
      sleep(SLEEP_INTERVAL);
      loopCount++;
    }
    printf("Exiting child process (pid: %u, loopCount = %u)\n", getpid(),
          loopCount);
    return EXIT_SUCCESS;
  // Otherwise, execution is in the context of the parent:
  // that process waits until the child completes and collects
  // the child's status.
  default:
    int childStatus;
    printf("Parent waiting for completion of child process (parent pid = %u, "
           "child pid = %u)\n",
           getpid(), childPid);
    waitpid(childPid, &childStatus, 0);

    printf("Child process exited, parent process is resuming execution (parent "
           "pid = %u, "
           "child pid = %u)\n",
           getpid(), childPid);
    if (WIFSIGNALED(childStatus)) {
      printf("Child process exit code (%u) indicates failure\n", childStatus);
      return EXIT_FAILURE;
    }
    printf("Child process exit code (%u) indicates successful completion\n", childStatus);
    return EXIT_SUCCESS;
  }
}
