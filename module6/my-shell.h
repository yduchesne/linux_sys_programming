#include "my-util.h"
#include "stdarg.h"
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>

#ifndef SA_RESTART
#define SA_RESTART 0x000004
#endif

#define PROMPT "my-sh > "
#define MAX_CMD_NAME_LEN 64
// The last argument in argv that we passed to execvp must be NULL.
// We are reserving 128 slots for the arguments, leaving the last one
// to the NULL value. We must therefore accept MAX_ARGS - 1 arguments.
#define MAX_ARGS 129
#define MAX_ARG_LEN 64
#define ARG_DELIM " "
#define CMD_EXIT "exit"

typedef enum _InputState { NOT_EMPTY = 0, EMPTY = 1, TIMED_OUT = 2 } InputState;

typedef struct _Command {
  char name[MAX_CMD_NAME_LEN];
  char args[MAX_ARGS][MAX_ARG_LEN];
  uint32_t argCount;
  uint32_t lineNumber;
} Command;

void printCmd(Command *cmd);

/**
 * Parses the given command line into a Command instance.
 *
 * Returns NULL if the command line is an empty command line, or
 * a Command pointer otherwise.
 */
Command *parseCommandLine(char *cmdLine, uint32_t lineNumber);

void executeCommand(Command *cmd);

static sigjmp_buf jmpBuf;

static void inactivity_handler(int signo);

InputState input(char *buf, uint32_t bufSz, FILE *inputStream,
                 uint32_t timeoutSecs);