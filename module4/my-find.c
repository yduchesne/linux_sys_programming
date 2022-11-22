/**
 * A custom find-like command leveraging C's standard library.
 *
 * my-find -p -t 5 *.c /home/alice/dev/projects
 *
 * Implementation Notes
 * ====================
 *
 * This program leverages the pthread API to perform directory traversal using
 * multiple threads:
 *
 * 1) The program allows specifying a number of threads (i.e.: dubbed thread
 *    capacity) to use (beyond the main thread) to perform the traversal. At
 *    the outset, the program has <capacity> available threads to work with,
 *    beyond the main thread.
 *
 * 2) Execution starts with the main thread: it lists the files/directories
 *    under the provided path. If the -r option (standing for "recursive")
 *    has been specified by the user, the main thread attempts dispatching
 *    the traversal of the next directory it encounters to a new thread. If
 *    all threads are busy, then the main thread traverses the next directory.
 *
 * 3) The process described in #2 continues recursively, and is identical for
 *    the threads started by the main thread, and started by "descendant"
 *    threads of the main thread: when a directory is encountered, the current
 *    thread attempts dispatching its traversal in a new thread. If all are
 *    busy at that moment, then it proceeds to the traversal itself.
 *
 * 4) The processing of files (matching their names against the provided
 *    pattern) is done in the current thread (i.e.: only upon encountering
 *    a directory is spawning a new thread attempted).
 *
 * The above process requires the use of a recursive mutex to keep track of
 * the running threads.
 *
 */
#define _DEFAULT_SOURCE

#include "dirent.h"
#include "errno.h"
#include "fnmatch.h"
#include "pthread.h"
#include "stdarg.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sys/stat.h"
#include "time.h"
#include "unistd.h"

// ============================================================================
// Macros and common types

// Used with lstat (which sets errno to 2 when a path isn't found
// on the file system)
#define ERRNO_NOT_FOUND 2

// Boolean values
#define TRUE 1
#define FALSE 0
typedef uint8_t bool;

#define LOG_LEVEL_NAME_LEN 50
#define MAX_THREADS 255
#define SLOT_MAIN_THREAD 255

// ----------------------------------------------------------------------------
// Utilities

/**
 *  Holds constants corresponding to the different log levels.
 */
typedef enum _LogLevel {
  TRACE = 0,
  VERBOSE = 1,
  NORMAL = 2,
  ERROR = 3,
  OFF = 4

} LogLevel;

/**
 * Logging function: outputs  only if the current level is >= than the system's
 * configured level.
 */
void logIt(LogLevel systemLevel, LogLevel currentLevel, char *format, ...) {
  if (currentLevel >= systemLevel) {
    va_list args;
    va_start(args, format);
    if (currentLevel == ERROR) {
      // making sure any error output is sent to
      // the terminal right away
      vfprintf(stderr, format, args);
    } else {
      vfprintf(stdout, format, args);
    }
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
    exit(EXIT_FAILURE);
  }
}

/**
 * Wraps malloc() to check for allocation failure (asserts if that's the case).
 */
void *safemalloc(size_t size) {
  void *ptr = malloc(size);
  assertIt(ptr != NULL, "Could not allocate memory\n");
  return ptr;
}

/**
 *  Wrap free() to check that the pointer to free isn't null.
 */
void safefree(void *ptr) {
  if (ptr != NULL) {
    free(ptr);
  }
}

// ----------------------------------------------------------------------------
// Threading

/**
 * Keeps track of a thread.
 */
typedef struct _ThreadRef {
  /**
   * ID of the thread to which this field corresonds - should be deemded
   * invalid is the isAvailable flag is true.
   */
  pthread_t thread;

  /**
   * Indicates whether or not the slot corresponding to the thread is available
   * or not. If it isn't available, it means that the thread corresponding to
   * this instance is currently active, and that the slot isn't available.
   * Otherwise, it means that the thread has been exited (and the thread ID
   * kept by the thread field is invalid).
   */
  bool isAvailable;
} ThreadRef;

/**
 * Program-wide structure (shared by all threads) holding the ThreadRef array
 * used to keep track of running threads and available thread slots (a slot
 * is simply a cell in the threadRefs array specified as a field of this
 * struct).
 *
 * Access to the threadRefs and availableThreadCount fields should be done in
 * a thread-safe manner, using the mutex that an instance of this struct
 * provides.
 */
typedef struct _ThreadState {
  // Used to synchronize access to the members of this struct.
  pthread_mutex_t mutex;
  pthread_mutexattr_t mutex_attr;
  ThreadRef threadRefs[MAX_THREADS];
  uint8_t threadCapacity;
  uint8_t availableThreadCount;
} ThreadState;

/**
 * Initializes a ThreadState instance.
 */
void newThreadState(ThreadState *ts, uint8_t threadCapacity) {

  assertIt(threadCapacity <= MAX_THREADS,
           "Specified thread capacity (%u) must be <= MAX_THREADS (%u)\n",
           threadCapacity, MAX_THREADS);
  ts->threadCapacity = threadCapacity;
  // Set to threadCapacity at the outset.
  ts->availableThreadCount = threadCapacity;

  for (int slot = 0; slot < ts->threadCapacity; slot++) {
    ThreadRef *ref = &ts->threadRefs[slot];
    ref->isAvailable = TRUE;
  }

  assertIt(pthread_mutexattr_init(&ts->mutex_attr) == 0,
           "Could not initialize mutex attributes\n");
  pthread_mutexattr_settype(&ts->mutex_attr, PTHREAD_MUTEX_RECURSIVE);
  assertIt(pthread_mutex_init(&ts->mutex, &ts->mutex_attr) == 0,
           "Could not initialize mutex\n");
}

/**
 * Releases the resources kept as part of the given ThreadState instance.
 */
void destroyThreadState(ThreadState *ts) {
  assertIt(pthread_mutexattr_destroy(&ts->mutex_attr) == 0,
           "Could not destroy mutex attributes\n");
  assertIt(pthread_mutex_destroy(&ts->mutex) == 0, "Could not destroy mutex\n");
}

// ----------------------------------------------------------------------------
// User input

/**
 * Holds user-defined settings (populated from command-line options).
 */
typedef struct _Settings {
  char pattern[NAME_MAX];
  bool isRecursive;
  LogLevel systemLogLevel;
} Settings;

/**
 * Initializes settings with defaults.
 */
void newSettings(Settings *settings) {
  strcpy(settings->pattern, "");
  settings->isRecursive = FALSE;
  settings->systemLogLevel = NORMAL;
}

// ----------------------------------------------------------------------------
// File metadata

/**
 * Holds directory/file metadata.
 */
typedef struct _FileInfo {
  // The full path to the file/directory to which this instance corresponds.
  char path[NAME_MAX];
  // The relative file name of the file/directory to which this instance
  // corrresponds.
  char name[NAME_MAX];
} FileInfo;

// ----------------------------------------------------------------------------
// File match callback

/**
 * Defines the signature of the callback that is invoked when
 * a matching file is found.
 */
typedef void (*FileMatchCallback)(const Settings *settings,
                                  const FileInfo *fileInfo);

/**
 * Implements the FileMatchCallback typedef.
 */
void outputMatch(const Settings *settings, const FileInfo *fileInfo) {

  int result = fnmatch(settings->pattern, fileInfo->name, FNM_PATHNAME);
  if (result == 0) {
    logIt(settings->systemLogLevel, NORMAL, "%s\n", fileInfo->path);
  } else {
    logIt(settings->systemLogLevel, TRACE,
          "No match for pattern %s against file path %s\n", settings->pattern,
          fileInfo->path);
  }
}

// ----------------------------------------------------------------------------
// VisitContext

/**
 * Encapsulates all parameters necessary for a visitDir function call in the
 * context of a specific thread.
 */
typedef struct _VisitContext {
  Settings *settings;
  FileInfo dirInfo;
  FileMatchCallback callback;
  ThreadState *threadState;
  int threadSlot;
} VisitContext;

// ============================================================================
// Core logic

// ----------------------------------------------------------------------------
// Directory traversal

/**
 * Forward declaration of function prototype.
 */
void startVisitThread(VisitContext *context);

/**
 * Visits the directory whose representation is encapsulated by the given
 * context. Calls startVisitThread whenever it encounters a sub-directory.
 */
uint8_t visitDir(VisitContext *context) {
  uint8_t exitCode = EXIT_SUCCESS;
  DIR *dir;
  struct dirent *entry;
  logIt(context->settings->systemLogLevel, VERBOSE,
        "visitDir -> directory: %s\n", context->dirInfo.path);
  if ((dir = opendir(context->dirInfo.path)) != NULL) {
    while ((entry = readdir(dir)) != NULL) {
      char fname[NAME_MAX] = "";

      strcat(fname, context->dirInfo.path);
      strcat(fname, "/");
      strcat(fname, entry->d_name);

      switch (entry->d_type) {
      case DT_REG:
      case DT_LNK:
        // TODO: check file match
        logIt(context->settings->systemLogLevel, TRACE, "Got file entry %s\n",
              fname);

        struct stat fileMeta;
        stat(fname, &fileMeta);
        FileInfo file;
        strncpy(file.path, fname, NAME_MAX);
        strncpy(file.name, entry->d_name, NAME_MAX);
        context->callback(context->settings, &file);
        break;
      case DT_DIR:
        if (context->settings->isRecursive &&
            strncmp(entry->d_name, ".", NAME_MAX) != 0 &&
            strncmp(entry->d_name, "..", NAME_MAX) != 0) {
          logIt(context->settings->systemLogLevel, TRACE,
                "Got directory entry %s\n", fname);

          VisitContext childContext;
          childContext.settings = context->settings;
          strncpy(childContext.dirInfo.path, fname, NAME_MAX);
          strncpy(childContext.dirInfo.name, entry->d_name, NAME_MAX);
          childContext.callback = context->callback;
          childContext.threadState = context->threadState;
          logIt(context->settings->systemLogLevel, TRACE,
                "Calling startVisitThread for directory entry %s\n", fname);
          startVisitThread(&childContext);
        }
        break;
      default:
        // ignoring other types
      }

      if (exitCode != EXIT_SUCCESS) {
        goto Finally;
      }
    }
  } else {
    logIt(context->settings->systemLogLevel, ERROR,
          "Could not access file or directory: %s\n", context->dirInfo.path);
  }

Finally:
  closedir(dir);
  return exitCode;
}

/**
 * Corresponds to the function pointer passed to the pthread_create call.
 * This function ensures that the proper book keeping is done when it has
 * completed its part of directory traversal.
 *
 * Namely, before call pthread_exit, this function releases the appropriate
 * thread slot (entry in the ThreadState::threadRefs table) and increments
 * ThreadState::availableThreadCount.
 *
 */
static void *runVisitThread(void *arg) {
  VisitContext *context = (VisitContext *)arg;
  logIt(context->settings->systemLogLevel, VERBOSE,
        "Calling visitDir in thread (slot #%d)\n", context->threadSlot);
  uint8_t status = visitDir(context);
  logIt(context->settings->systemLogLevel, VERBOSE,
        "visitDir completed by thread (slot #%d) - status: %u\n",
        context->threadSlot, status);

  int lockStatus = pthread_mutex_lock(&(context->threadState->mutex));
  assertIt(lockStatus == 0,
           "Error trying to lock thread state mutex (status: %d)\n",
           lockStatus);
  context->threadState->availableThreadCount += 1;
  logIt(context->settings->systemLogLevel, VERBOSE,
        "Available thread count now at %u\n",
        context->threadState->availableThreadCount);

  ThreadRef *ref = &(context->threadState->threadRefs[context->threadSlot]);
  ref->isAvailable = TRUE;
  int unlockStatus = pthread_mutex_unlock(&(context->threadState->mutex));
  assertIt(unlockStatus == 0,
           "Error trying to unlock thread state mutex (status: %d)\n",
           unlockStatus);
  logIt(context->settings->systemLogLevel, TRACE,
        "Freeing up VisitContext for thread (slot #%u)\n", context->threadSlot);
  if (context->threadSlot != SLOT_MAIN_THREAD) {
    safefree(context);
    pthread_exit(NULL);
  } else {
    safefree(context);
  }

  return NULL;
}

/**
 * This function attempts to perform the next visit in a new thread: if
 * all the thread slots are busy (i.e.: the number of active threads is
 * currently at capacity), then the next visit is performed by the
 * calling thread.
 *
 * Otherwise, the function starts a new thread, making sure the proper
 * bookkeeping is done (marking the corresponding slot has unavailable,
 * decrementing the available thread count).
 */
void startVisitThread(VisitContext *context) {
  assertIt(context != NULL, "startVisitThread:: VisitContext is NULL\n");
  logIt(context->settings->systemLogLevel, TRACE,
        "Acquiring thread state mutex lock\n");
  int lockStatus = pthread_mutex_lock(&(context->threadState->mutex));
  assertIt(lockStatus == 0,
           "Error trying to lock thread state mutex (status: %d)\n",
           lockStatus);
  logIt(context->settings->systemLogLevel, TRACE,
        "Acquired thread state mutex lock\n");
  if (context->threadState->availableThreadCount > 0) {
    logIt(context->settings->systemLogLevel, VERBOSE,
          "Will handle directory %s in another thread\n",
          context->dirInfo.path);

    int slot = 0;
    // finding an available slot
    while (slot < context->threadState->threadCapacity) {
      ThreadRef *ref = &(context->threadState->threadRefs[slot]);
      if (ref->isAvailable) {
        logIt(context->settings->systemLogLevel, TRACE,
              "Found available slot #%d\n", slot);
        break;
      }
      slot++;
    }

    // The following condition should never be true since
    // since availableThreadCont > 0, then we should have
    // slots available. Nevertheless, we're taking it into
    // account, for robustness' sake.
    if (slot == context->threadState->threadCapacity) {
      logIt(context->settings->systemLogLevel, VERBOSE,
            "Could not find available slot for thread expected to handle %s "
            "(thread capacity: %u, "
            "available: %u)\n",
            context->dirInfo.path, context->threadState->threadCapacity,
            context->threadState->availableThreadCount);
      goto VisitInCurrentThread;
    }

    // Making copy of parent context (the parent context exists
    // on the stack of the calling function, so we need to copy it).
    VisitContext *threadContext =
        (VisitContext *)safemalloc(sizeof(VisitContext));
    threadContext->callback = context->callback;
    strncpy(threadContext->dirInfo.path, context->dirInfo.path,
            sizeof(context->dirInfo.path));
    strncpy(threadContext->dirInfo.name, context->dirInfo.name,
            sizeof(context->dirInfo.name));
    threadContext->settings = context->settings;
    threadContext->threadState = context->threadState;
    ThreadRef *threadRef = &(threadContext->threadState->threadRefs[slot]);
    threadRef->isAvailable = FALSE;

    threadContext->threadSlot = slot;
    threadContext->threadState->availableThreadCount -= 1;
    int status = pthread_create(&threadRef->thread, NULL, runVisitThread,
                                (void *)threadContext);
    assertIt(status == 0, "Error creating thread (status: %d)\n", status);
    logIt(
        context->settings->systemLogLevel, VERBOSE,
        "Started new thread for slot #%d (available thread count now at: %u)\n",
        slot, threadContext->threadState->availableThreadCount);
    int unlockStatus = pthread_mutex_unlock(&(context->threadState->mutex));
    assertIt(unlockStatus == 0,
             "Error trying to unlock thread state mutex (status: %d)\n",
             lockStatus);
    return;
  }
VisitInCurrentThread:
  int unlockStatus = pthread_mutex_unlock(&(context->threadState->mutex));
  assertIt(unlockStatus == 0,
           "Error trying to unlock thread state mutex (status: %d)\n",
           lockStatus);
  visitDir(context);
}

// ----------------------------------------------------------------------------
// help & main

void help(const char *programName) {
  printf("%s [-p <pattern>] [-r] [-l <log level>] [<path>]\n", programName);
  printf("  -p: glob pattern to use for matching files (defaults to *)\n");
  printf("  -r: indicates that the traversal should be recursive\n");
  printf("  -l: indicates the log level (defaults to normal).\n");
  printf("      Possible values, from most verbose to least verbose:\n");
  printf("      - trace\n");
  printf("      - verbose\n");
  printf("      - normal\n");
  printf("      - error\n");
  printf("      - off\n");
}

int main(int argc, char **argv) {
  // exit code
  uint8_t exitCode = EXIT_SUCCESS;

  // path set to current dir by default
  char path[NAME_MAX];
  strcpy(path, ".");

  // populated from command-line options
  Settings settings;
  newSettings(&settings);
  ThreadState threadState;
  // Defaulting to 0 additional threads (all
  // will be executed in the main thread).
  newThreadState(&threadState, 0);

  // option processing
  int opt;
  while ((opt = getopt(argc, argv, "hrp:l:t:")) != -1) {
    switch (opt) {
    case 'h':
      help(argv[0]);
      exitCode = EXIT_SUCCESS;
      goto Finally;
    case 'p':
      strncpy(settings.pattern, optarg, sizeof(settings.pattern));
      break;
    case 'r':
      settings.isRecursive = TRUE;
      break;
    case 'l':
      if (strncmp(optarg, "off", LOG_LEVEL_NAME_LEN) == 0) {
        settings.systemLogLevel = OFF;
      } else if (strncmp(optarg, "error", LOG_LEVEL_NAME_LEN) == 0) {
        settings.systemLogLevel = ERROR;
      } else if (strncmp(optarg, "normal", LOG_LEVEL_NAME_LEN) == 0) {
        settings.systemLogLevel = NORMAL;
      } else if (strncmp(optarg, "verbose", LOG_LEVEL_NAME_LEN) == 0) {
        settings.systemLogLevel = VERBOSE;
      } else if (strncmp(optarg, "trace", LOG_LEVEL_NAME_LEN) == 0) {
        settings.systemLogLevel = TRACE;
      } else {
        fprintf(stderr, "Unknown log level: %s\n", optarg);
        exitCode = EXIT_FAILURE;
        goto Finally;
      }

      break;
    case 't':
      int threadCapacity = atoi(optarg);
      assertIt(threadCapacity > 0,
               "Value of -t option (thread capacity) must be > 0. Got: %d\n",
               threadCapacity);
      logIt(settings.systemLogLevel, VERBOSE,
            "Setting thread capacity to: %d\n", threadCapacity);
      newThreadState(&threadState, (uint8_t)threadCapacity);
    }
  }

  // if there's a remaining arg, use as path
  if (optind < argc) {
    strncpy(path, argv[optind], NAME_MAX);
  }

  if (strnlen(settings.pattern, sizeof(settings.pattern)) == 0) {
    logIt(settings.systemLogLevel, ERROR, "Pattern (-p) must be provided\n");
    exitCode = EXIT_FAILURE;
    goto Finally;
  }

  // Obtaining metadata for path (used to determine whether
  // it is a file or a directory)
  struct stat pathInfo;
  if (lstat(path, &pathInfo) == -1) {
    switch (errno) {
    case ERRNO_NOT_FOUND:
      logIt(settings.systemLogLevel, ERROR, "No such directory or file: %s\n",
            path);
      break;
    default:
      logIt(settings.systemLogLevel, ERROR,
            "Error accessing directory or file: %s (errno: %u)\n", path, errno);
    }
    exitCode = EXIT_FAILURE;
    goto Finally;
  }

  logIt(settings.systemLogLevel, VERBOSE,
        "Starting traversal at directory: %s\n", path);
  logIt(settings.systemLogLevel, VERBOSE, "Pattern: %s\n", settings.pattern);
  if (settings.isRecursive) {
    logIt(settings.systemLogLevel, VERBOSE,
          "Will perform recursive traversal\n");
  }

  if (S_ISDIR(pathInfo.st_mode)) {
    VisitContext initialContext;
    initialContext.settings = &settings;
    strncpy(initialContext.dirInfo.path, path, NAME_MAX);
    initialContext.callback = outputMatch;
    initialContext.threadState = &threadState;
    initialContext.threadSlot = SLOT_MAIN_THREAD;
    visitDir(&initialContext);

    logIt(settings.systemLogLevel, VERBOSE,
          "Waiting on active threads to complete...\n");
    for (int slot = 0; slot < initialContext.threadState->threadCapacity;
         slot++) {
      logIt(settings.systemLogLevel, TRACE,
            "Checking if thread for slot #%d is active and should be joined\n",
            slot);
      /*int lockStatus = pthread_mutex_lock(&initialContext.threadState->mutex);
      assertIt(lockStatus == 0, "Could not lock mutex\n");*/
      ThreadRef *threadRef = &(initialContext.threadState->threadRefs[slot]);
      if (!threadRef->isAvailable) {
        /*
        int unlockStatus =
            pthread_mutex_unlock(&initialContext.threadState->mutex);

        assertIt(unlockStatus == 0, "Could not unlock mutex\n");
        */
        pthread_t thread = threadRef->thread;
        logIt(settings.systemLogLevel, TRACE, "Joining thread for slot #%d\n",
              slot);
        pthread_join(thread, NULL);
      }
    }
    logIt(settings.systemLogLevel, VERBOSE, "All active threads done\n");
    destroyThreadState(&threadState);
  } else {
    logIt(settings.systemLogLevel, ERROR,
          "Invalid file type for: %s (expected path to directory)\n", path);
    exitCode = EXIT_FAILURE;
  }

// Catch-all: terminates the process
Finally:
  exit(exitCode);
}