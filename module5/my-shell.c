#include "stdarg.h"
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SA_RESTART
#define SA_RESTART 0x000004
#endif

#define INPUT_TIMEOUT_SECS 60
#define PROMPT "my-sh > "
#define MAX_CMD_LINE_LEN 512
#define MAX_CMD_NAME_LEN 64
// The last argument in argv that we passed to execvp must be NULL.
// We are reserving 128 slots for the arguments, leaving the last one
// to the NULL value. We must therefore accept MAX_ARGS - 1 arguments.
#define MAX_ARGS 129
#define MAX_ARG_LEN 64
#define ARG_DELIM " "
#define CMD_EXIT "exit"

// Boolean values
#define TRUE 1
#define FALSE 0
typedef uint8_t bool;

typedef struct sigation SigAction;

void *safeMalloc(size_t size, const char *hint) {
  void *ptr = malloc(size);
  if (ptr == NULL) {
    fprintf(stderr, "Could not allocate memory (hint: %s)\n", hint);
    _exit(EXIT_FAILURE);
  }
  return ptr;
}

void safeFree(void *ptr) {
  if (ptr != NULL) {
    free(ptr);
  }
}

/**
 * Assertion utility.
 */
void assertIt(bool condition, char *format, ...) {
  if (!condition) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    _exit(EXIT_FAILURE);
  }
}

/**
 * Returns the position of the first EOL character found
 * in the given input.
 *
 * The goal of this function is to allow for replacing
 * with a '\0' the EOF character at the located position.
 */
int findEndOfLine(const char *input, uint32_t maxLen) {
  for (int i = 0; i < maxLen; i++) {
    if (input[i] == '\n' || input[i] == '\0') {
      return i;
    }
  }
  return maxLen;
}

typedef enum _InputState { NOT_EMPTY = 0, EMPTY = 1, TIMED_OUT = 2 } InputState;

typedef struct _Command {
  char name[MAX_CMD_NAME_LEN];
  char args[MAX_ARGS][MAX_ARG_LEN];
  uint32_t argCount;
  uint32_t lineNumber;
} Command;

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

static sigjmp_buf jmpBuf;

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
      int eol = findEndOfLine(buf, MAX_CMD_LINE_LEN);
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

int main(int argc, char **argv) {
  char cmdLine[MAX_CMD_LINE_LEN];
  uint32_t lineNumber = 0;
  FILE *inputStream = stdin;
  bool tty = isatty(fileno(inputStream));
  char *prompt = tty ? PROMPT : "";

  if (tty) {
    fprintf(stderr, "%s", prompt);
  }

  while (input(cmdLine, MAX_CMD_LINE_LEN, inputStream, INPUT_TIMEOUT_SECS) !=
         TIMED_OUT) {
    lineNumber++;
    Command *cmd = parseCommandLine(cmdLine, lineNumber);
    if (cmd != NULL) {
      executeCommand(cmd);
      safeFree(cmd);
    } else {
      fprintf(stderr, "No command specified\n");
    }
    if (tty) {
      fprintf(stderr, "\n%s", prompt);
    }
  }
  // If we reach this point, it is because of an
  // input timeout
  fprintf(stderr, "No activity detected for at least %u seconds. Exiting.\n",
          INPUT_TIMEOUT_SECS);
  _exit(EXIT_SUCCESS);
}