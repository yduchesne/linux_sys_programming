/**
 * A custom ls-like command leveraging C's standard library.
 *
 */

// Macro required to be able to use the constants corresponding to the 
// different values that dirent.d_type can take: DT_REG, DT_DIR, etc.
#define _DEFAULT_SOURCE

#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "errno.h"
#include "stdint.h"
#include "sys/stat.h"
#include "dirent.h"


// Return codes
#define S_OK 0
#define S_ERR 1

// File type chars
#define FILE_TYPE 'f'
#define DIR_TYPE 'd'
#define UNKNOWN_TYPE 'u'

#define ERRNO_NOT_FOUND 2


void help()
{
    printf("ls [<path>]");
}

int processFile(const char* path, const struct stat *fileInfo)
{
    printf("f\t%s (size: %zu bytes)\n", path, fileInfo->st_size);
    return S_OK;
}

int processDir(const char* path, const struct stat *dirInfo)
{
    DIR *dir;
    struct dirent *entry;

    uint32_t numFiles = 0;
    uint32_t numDirs = 0;

    if ( ( dir = opendir(path) ) != NULL)
    {
        while ( ( entry = readdir(dir) ) != NULL )
        {
            // Ignore the current and parent dirs
            if ( 
                ( strcmp(entry->d_name, ".") == 0 ) || 
                ( strcmp(entry->d_name, "..") == 0 ) 
            )
            {
                continue;
            }

            switch(entry->d_type)
            {
                case DT_REG:
                    char fname[NAME_MAX] = "";

                    strcat(fname, path);
                    strcat(fname, "/");
                    strcat(fname, entry->d_name);

                    struct stat fileInfo;
                    stat(fname, &fileInfo);
                    processFile(fname, &fileInfo);
                    numFiles++;
                    break;
                case DT_DIR:
                    numDirs++;
                    printf("d\t%s\n", entry->d_name);
                    break;
            }
           
        }
        
    }
    else 
    {
        fprintf(stderr, "No such directory: %s\n", path);
        return S_ERR;
    }
    closedir(dir);
    printf("Total...........: %u\n", numFiles + numDirs);
    printf("  Files.........: %u\n", numFiles);
    printf("  Directories...: %u\n", numDirs);    
    return S_OK;
}

int main(int argc, char** argv)
{
    // Validating input
    char* path = ".";
    int result = S_ERR;

    if ( argc > 1 ) 
    {
        path = argv[1];        
        // Displaying help if requested
        if ( 
            ( strcmp(path, "-h") == 0 ) || 
            ( strcmp(path, "-help") == 0 ) ||         
            ( strcmp(path, "--help") == 0 )
        )  
        {
            help();
            return S_OK;
        }
    }

    // Obtaining metadata for path (used to determine whether
    // it is a file or a directory)
    struct stat pathInfo;
    if (stat(path, &pathInfo) == -1)
    {
        switch(errno)
        {
            case ERRNO_NOT_FOUND:
                fprintf(stderr, "No such directory or file: %s", path);   
                break;
            default:
                fprintf(stderr, "Error accessing directory or file: %s (errno: %u)", path, errno);            
        }
        result = S_ERR;
        goto Finally;
    }

    int isFile = S_ISREG(pathInfo.st_mode);
    int isDir  = S_ISDIR(pathInfo.st_mode);
    if (isFile)
    {
        result = processFile(path, &pathInfo);
        goto Finally;
    } 
    else if (isDir)
    {
        result = processDir(path, &pathInfo);
        goto Finally;
    }
    else
    {
        fprintf(stderr, "Invalid file type for: %s (expected path to directory of file)", path);
        result = S_ERR;
        goto Finally;
    }

    // Catch-all: terminates the process
    Finally:
        exit(result);
  
}
