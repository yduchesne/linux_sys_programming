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

// Others
#define DIR_ENTRY_NAME_MAX_LEN 256

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
                    char fname[DIR_ENTRY_NAME_MAX_LEN] = "";

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
        printf("No such directory: %s\n", path);
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

    if ( argc > 1 ) 
    {
        path = argv[1];        
        // Lenient handling of help option
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
    struct stat pathInfo;
    stat(path, &pathInfo);

    int isFile = S_ISREG(pathInfo.st_mode);
    int isDir  = S_ISDIR(pathInfo.st_mode);
    if (isFile)
    {
        return processFile(path, &pathInfo);
    } 
    else if (isDir)
    {
        return processDir(path, &pathInfo);
    }
    else 
    {
        printf("Invalid argument: %s (no such directory or file)", path);
        return S_ERR;
    }
  
}
