#include "my-util.h"
#include "stdarg.h"
#include <stdio.h>
#include <stdlib.h>

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

void assertIt(bool condition, char *format, ...) {
  if (!condition) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    _exit(EXIT_FAILURE);
  }
}

int findEndOfLine(const char *input, uint32_t maxLen) {
  for (int i = 0; i < maxLen; i++) {
    if (input[i] == '\n' || input[i] == '\0') {
      return i;
    }
  }
  return maxLen;
}