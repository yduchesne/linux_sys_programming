#include "my-shell.h"
#include <stdio.h>
#include <stdlib.h>

#define INPUT_TIMEOUT_SECS 60
#define MAX_CMD_LINE_LEN 512

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