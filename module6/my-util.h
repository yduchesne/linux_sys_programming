#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

// Boolean values
#define TRUE 1
#define FALSE 0
typedef uint8_t bool;

/**
 * Internally asserts that memory has been allocated. Allows
 * passing in a hint, used in the output, which can help debugging.
 */
void *safeMalloc(size_t size, const char *hint);

/**
 * Internally calls free if the given pointer isn't NULL.
 */
void safeFree(void *ptr);

/**
 * Assertion utility.
 */
void assertIt(bool condition, char *format, ...);

/**
 * Returns the position of the first EOL character found
 * in the given input.
 *
 * The goal of this function is to allow for replacing
 * with a '\0' the EOF character at the located position.
 */
int findEndOfLine(const char *input, uint32_t maxLen);