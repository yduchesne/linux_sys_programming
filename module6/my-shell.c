#include "my-shell.h"
#include "my-util.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>

void printCmd(Command *cmd) {
  printf("%s", cmd->name);

  // First argument is command name, which we've already printed above.
  if (cmd->argCount > 1) {
    for (uint32_t i = 1; i < cmd->argCount; i++) {
      printf(" %s", cmd->args[i]);
    }
  }
  printf("\n");
}

/**
 * Parses the given command line into a Command instance.
 *
 * Returns NULL if the command line is an empty command line, or
 * a Command pointer otherwise.
 */
Command *parseCommandLine(char *cmdLine, uint32_t lineNumber) {
  char *token = NULL;
  uint32_t tokenCount = 0;

  Command *cmd =
      (Command *)safeMalloc(sizeof(Command), "Creating Command instance");
  cmd->lineNumber = lineNumber;

  do {
    token =
        tokenCount == 0 ? strtok(cmdLine, ARG_DELIM) : strtok(NULL, ARG_DELIM);
    if (token != NULL) {
      assertIt(tokenCount < MAX_ARG_LEN - 1,
               "Too many arguments. Expected max of: %u", MAX_ARG_LEN - 1);
      // Treating the first token as the command name
      if (tokenCount == 0) {
        strncpy(cmd->name, token, sizeof(cmd->name));
      }

      strncpy(cmd->args[tokenCount], token, sizeof(cmd->args[tokenCount]));
      tokenCount++;
    }
  } while (token != NULL);

  cmd->argCount = tokenCount;

  // Command line was empty
  // - returning NULL in that case
  if (tokenCount == 0) {
    safeFree(cmd);
    return NULL;
  }

  return cmd;
}

void executeCommand(Command *cmd) {
  if (strcmp(cmd->name, CMD_EXIT) == 0) {
    _exit(EXIT_SUCCESS);
  }
  pid_t cmdPid = fork();
  assertIt(cmdPid >= 0, "Could not fork command process");

  // If cmdPid == 0, then we're in the child process' context
  if (cmdPid == 0) {
    int exitCode = execvp(cmd->name, (char *const *)cmd->args);
    if (exitCode == 1) {
      fprintf(stderr, "Error executing command (errno: %u) at line %u", errno,
              cmd->lineNumber);
      printCmd(cmd);
      _exit(errno);
    }
  }
  // else, we are in the parent's context and need to wait
  // for the child to finish
  else {
    int childStatus;
    wait(&childStatus);
    if (childStatus != 0) {
      // Command exection resulted in an error,
      // aborting
      _exit(childStatus);
    }
  }
}

static void inactivity_handler(int signo) { siglongjmp(jmpBuf, 1); }

InputState input(char *buf, uint32_t bufSz, FILE *inputStream,
                 uint32_t timeoutSecs) {

  int previousAlarm;
  InputState state = NOT_EMPTY;

  struct sigaction currentAction = {0};
  struct sigaction previousAction = {0};

  if (sigsetjmp(jmpBuf, 1) == 0) {
    currentAction.sa_handler = inactivity_handler;
    sigemptyset(&(currentAction.sa_mask));
    currentAction.sa_flags = SA_RESTART;

    previousAlarm = alarm(0);
    sigaction(SIGALRM, &currentAction, &previousAction);

    alarm(timeoutSecs);

    if (fgets(buf, bufSz, inputStream) != NULL) {
      int eol = findEndOfLine(buf, bufSz);
      buf[eol] = '\0';
      state = NOT_EMPTY;
    } else {
      state = EMPTY;
    }
  } else {
    state = TIMED_OUT;
  }

  // Resetting alarm state
  alarm(0);
  sigaction(SIGALRM, &previousAction, 0);
  alarm(previousAlarm);

  return state;
}
