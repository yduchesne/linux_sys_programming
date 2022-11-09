/**
 * A custom ls-like command leveraging C's standard library.
 *
 * The command outputs data in greppable format. Example:
 *
 * my-ls -a | sort | grep "type: file"
 *
 */

// Macro required to be able to use the constants corresponding to the
// different values that dirent.d_type can take: DT_REG, DT_DIR, etc.
#define _DEFAULT_SOURCE

#include "dirent.h"
#include "errno.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sys/stat.h"
#include "time.h"
#include "unistd.h"

// ============================================================================
// Macros, typedefs, helper functions

// Exit codes
#define EC_OK 0
#define EC_ERR 1
#define EC_INVALID_FILE_TYPE 2

// Used with lstat (which sets errno to 2 when a path isn't found
// on the file system)
#define ERRNO_NOT_FOUND 2

// Boolean values
#define TRUE 1
#define FALSE 0
typedef uint8_t bool;

// Formatted time type
#define TIME_STR_LEN 50
typedef char TIME_STR[TIME_STR_LEN];

// Formats time
void formatTime(time_t timestamp, TIME_STR buffer)
{
    struct tm tm_info;
    localtime_r(&timestamp, &tm_info);
    strftime(buffer, TIME_STR_LEN, "%Y-%m-%dT%H.%M.%S", &tm_info);
}

// Identifies supported file types
typedef enum _FileType
{
    TYPE_FILE = 0,
    TYPE_DIR = 1,
    TYPE_LINK = 2,
    UNDEFINED_FILE_TYPE = 100
} FileType;

FileType getFileType(mode_t mode)
{
    FileType fileType = UNDEFINED_FILE_TYPE;

    if (S_ISREG(mode))
    {
        fileType = TYPE_FILE;
    }
    else if (S_ISDIR(mode))
    {
        fileType = TYPE_DIR;
    }
    else if (S_ISLNK(mode))
    {
        fileType = TYPE_LINK;
    }

    return fileType;
}

// Holds user-defined settings
// (populated from command-line options)
typedef struct _Settings
{
    bool with_disk_info;
    bool with_owner_info;
    bool with_perm_info;
    bool with_time_info;

} Settings;

// initializes settings with defaults
void newSettings(Settings *settings)
{
    settings->with_disk_info = FALSE;
    settings->with_owner_info = FALSE;
    settings->with_perm_info = FALSE;
    settings->with_time_info = FALSE;
}

void help(const char *programName)
{
    printf("%s [-adopt] [<path>]\n", programName);
    printf("  -a: all info (equivalent to -dopt)\n");
    printf("  -d: disk info\n");
    printf("  -o: owner info\n");
    printf("  -p: permission info\n");
    printf("  -t: time info\n");
}

// ============================================================================
// Core functionality

uint8_t processFile(FileType fileType, const Settings *settings, const char *path, const struct stat *fileInfo)
{
    char typeName[NAME_MAX];
    switch (fileType)
    {
    case TYPE_FILE:
        strcpy(typeName, "file");
        break;
    case TYPE_LINK:
        strcpy(typeName, "link");
        break;
    case TYPE_DIR:
        strcpy(typeName, "dir");
        break;
    default:
        fprintf(stderr, "Invalid file type: %s", path);
        return EC_INVALID_FILE_TYPE;
    }

    printf("name: %s, type: %s", path, typeName);

    if (settings->with_owner_info)
    {
        printf(", uid: %u, gid: %u", fileInfo->st_uid, fileInfo->st_gid);
    }

    if (settings->with_disk_info)
    {
        printf(", inode: %lu, blocks: %zu, block_size: %zu, size: %zu", fileInfo->st_ino, fileInfo->st_blocks,
               fileInfo->st_blksize, fileInfo->st_size);
    }

    if (settings->with_perm_info)
    {
        printf(", u: ");
        printf((fileInfo->st_mode & S_IRUSR) ? "r" : "-");
        printf((fileInfo->st_mode & S_IWUSR) ? "w" : "-");
        printf((fileInfo->st_mode & S_IXUSR) ? "x" : "-");

        printf(", g: ");
        printf((fileInfo->st_mode & S_IRGRP) ? "r" : "-");
        printf((fileInfo->st_mode & S_IWGRP) ? "w" : "-");
        printf((fileInfo->st_mode & S_IXGRP) ? "x" : "-");

        printf(", o: ");
        printf((fileInfo->st_mode & S_IROTH) ? "r" : "-");
        printf((fileInfo->st_mode & S_IWOTH) ? "w" : "-");
        printf((fileInfo->st_mode & S_IXOTH) ? "x" : "-");
    }

    if (settings->with_time_info)
    {
        TIME_STR created;
        TIME_STR modified;
        TIME_STR accessed;

        formatTime(fileInfo->st_ctim.tv_sec, created);
        formatTime(fileInfo->st_mtim.tv_sec, modified);
        formatTime(fileInfo->st_atim.tv_sec, accessed);

        printf(", created: %s, modified: %s, accessed: %s", created, modified, accessed);
    }

    printf("\n");
    return EC_OK;
}

uint8_t processDir(const Settings *settings, const char *path, const struct stat *dirInfo)
{
    uint8_t exitCode = EC_OK;
    DIR *dir;
    struct dirent *entry;
    if ((dir = opendir(path)) != NULL)
    {
        while ((entry = readdir(dir)) != NULL)
        {
            char fname[NAME_MAX] = "";

            strcat(fname, path);
            strcat(fname, "/");
            strcat(fname, entry->d_name);

            struct stat fileInfo;
            stat(fname, &fileInfo);

            switch (entry->d_type)
            {
            case DT_REG:
            case DT_LNK:
                exitCode = processFile(getFileType(fileInfo.st_mode), settings, fname, &fileInfo);
                break;
            case DT_DIR:
                exitCode = processFile(TYPE_DIR, settings, entry->d_name, &fileInfo);
                break;
            default:
                // ignoring other types
            }

            if (exitCode != EC_OK)
            {
                goto Finally;
            }
        }
    }
    else
    {
        fprintf(stderr, "No such directory: %s\n", path);
        exitCode = EC_ERR;
    }

Finally:
    closedir(dir);
    return exitCode;
}

int main(int argc, char **argv)
{
    // exit code
    uint8_t exitCode = EC_OK;

    // path set to current dir by default
    char path[NAME_MAX];
    strcpy(path, ".");

    // populated from command-line options
    Settings settings;
    newSettings(&settings);

    // option processing
    int opt;
    while ((opt = getopt(argc, argv, ":hadopt :")) != -1)
    {
        switch (opt)
        {
        case 'h':
            help(argv[0]);
            exitCode = EC_OK;
            goto Finally;
        case 'a':
            settings.with_disk_info = TRUE;
            settings.with_owner_info = TRUE;
            settings.with_perm_info = TRUE;
            settings.with_time_info = TRUE;
            break;
        case 'd':
            settings.with_disk_info = TRUE;
            break;
        case 'o':
            settings.with_owner_info = TRUE;
            break;
        case 'p':
            settings.with_perm_info = TRUE;
            break;
        case 't':
            settings.with_time_info = TRUE;
            break;
        }
    }

    // if there's a remaining arg, use as path
    if (optind < argc)
    {
        strncpy(path, argv[optind], NAME_MAX);
    }

    // Obtaining metadata for path (used to determine whether
    // it is a file or a directory)
    struct stat pathInfo;
    if (lstat(path, &pathInfo) == -1)
    {
        switch (errno)
        {
        case ERRNO_NOT_FOUND:
            fprintf(stderr, "No such directory or file: %s", path);
            break;
        default:
            fprintf(stderr, "Error accessing directory or file: %s (errno: %u)", path, errno);
        }
        exitCode = EC_ERR;
        goto Finally;
    }

    FileType fileType = getFileType(pathInfo.st_mode);

    switch (fileType)
    {
    case TYPE_FILE:
    case TYPE_LINK:
        exitCode = processFile(fileType, &settings, path, &pathInfo);
        break;

    case TYPE_DIR:
        exitCode = processDir(&settings, path, &pathInfo);
        break;

    default:
        fprintf(stderr, "Invalid file type for: %s (expected path to directory of file)", path);
        exitCode = EC_ERR;
    }

// Catch-all: terminates the process
Finally:
    exit(exitCode);
}
