/*

3. Write a C application “writer” (finder-app/writer.c)  which can be used as an alternative to the “writer.sh” test 
   script created in assignment1 and using File IO as described in LSP chapter 2.  See the Assignment 1 requirements 
   for the writer.sh test script and these additional instructions:

  * One difference from the write.sh instructions in Assignment 1:  You do not need to make your "writer" utility 
     create directories which do not exist.  You can assume the directory is created by the caller.
  * Setup syslog logging for your utility using the LOG_USER facility.
  * Use the syslog capability to write a message “Writing <string> to <file>” where <string> is the text string written 
     to file (second argument) and <file> is the file created by the script.  This should be written with LOG_DEBUG 
     level.
  * Use the syslog capability to log any unexpected errors with LOG_ERR level.

*/

/*
 * References used:
 *  * Linux System Programming, 2nd edition, by Robert Love. O'Reily, May 2013.
 *  * 21st Century C, 2nd edition, by Ben Klemens. O'Reilly, September 2014.
 *  * The GNU C Library Reference Manual, for version 2.38, by the Free Software Foundation. 
 *    Online, retreived Nov 29 2023 from 
 *    https://www.gnu.org/software/libc/manual/html_node/index.html
 *  * Linux Programmer's Manual (manpages).
 * 
 * No examples were used, only the API descriptions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <syslog.h>
#include "writer.h"

int main(int argc, char *argv[]) {

    openlog("writer",SYSLOG_OPTIONS,SYSLOG_FACILITY);

    if(validate_args(argc))
        return 1; // failed validation
    
    char *filepath = argv[1];
    char *textstring = argv[2];

    syslog(LOG_DEBUG,"Writing %s to %s",textstring,filepath);

    char *textline = malloc(strlen(textstring) + 2);
    textline = strcat(textstring, "\n");

    if (mk_dir_r(filepath)) {
        syslog(LOG_ERR,"main: mk_dir_r: %m");
        return 1;
    }

    int desc = open_file(filepath); 
    if(desc == -1) {
        syslog(LOG_ERR,"main: open_file: %m");
        return 1;
    }

    if(write(desc, textline, strlen(textline)+1) == -1) {
        syslog(LOG_ERR,"main: write: %m");
        return 1;
    }
    
    if(close_file(desc)) {
        syslog(LOG_ERR,"main: close_file: %m");
        return 1;
    }

    closelog();

    return 0;
}

/* Validates that the argument count is correct for the usage of the program.
 * If the argument count is not 3 (that is, 2 arguments passed), the function
 * prints usage information and returns a non-zero value.
 * 
 * Parameters:
 *   int argc   number of arguments passed.
 * Returns:
 *   0 on sucess
 *   Others on error.
 */
int validate_args(int argc) {
    if (argc != 3 ) {
        printf("Usage: writer.sh [filepath] [textstring]\n");
        printf("Example invocation: writer.sh /tmp/aesd/assignment1/sample.txt ios\n");
        return 1;
    }
    else {
        return 0;
    }
}

/* Opens the specified file. If this file does not exist, the file is created 
 * in the directory where it belongs. If  *  that directory does not exist, it 
 * is created. If the directory containing that directory does not exist, it is 
 * created, etc.
 *
 *  Parameters:
 *    char *path pointer to a string containing the path of the file. Examples:
 *               /home/ivan/testfolder1/myfile.txt
 *               "/home/ivan/aesd tests/test/file2.txt"
 *  Returns:
 *    int file descriptor
 *   You may want to check errno for the open() function call error.
 */
int open_file(char *path) {
    int errOpen = 0;
    int fileDesc = open(path, FLAGS_OPEN, MODE_OPEN);
    if (fileDesc == -1) {
        errOpen = errno;
        if (errOpen == ENOENT) {    //"no such file or directory"
            // Create directories recursively
            mk_dir_r(path);
            // Try to open again
            fileDesc = open(path, FLAGS_OPEN, MODE_OPEN);
            errOpen = errno;
            if(fileDesc == -1)
                syslog(LOG_ERR,"open_file: open() after path directory creation: %m");
        }
        else {
            syslog(LOG_ERR,"open_file: open() before path directory creation: %m");
        }
    }
    errno = errOpen;
    return fileDesc;
}

/* Closes the specified file. Wrapper for close()
 * Parameters:
 *   int desc   file descriptor
 * Returns
 *   zero on success. On error, -1 is returned and errno is set appropriately.
 */
inline int close_file(int desc) {
    return (close(desc));
}

/* Creates the specified directory. 
 *  Parameters:
 *    char *path pointer to a string containing the path of the dir. Examples:
 *               /home/ivan/testfolder1/
 *               "/home/ivan/aesd tests/test/"
 *  Returns:
 *     0  on success
 *    -1  on unspecified error
 *   You may want to check errno for the last system call error.
 */
inline int mk_dir(const char *path) {
    return mkdir(path, MODE_MKDIR);
}

/* Recursively creates the specified directory. 
 *  Parameters:
 *    char *path pointer to a string containing the path of the dir. Examples:
 *               /home/ivan/testfolder1/
 *               "/home/ivan/aesd tests/test/"
 *  Returns:
 *     0  on success
 *    -1  on unspecified error
 *   You may want to check errno for the last system call error.
 */
int mk_dir_r(const char *path) {
    // Hold on to your seats everyone, we're about to deal with malloc 🙃
    // The reason for working on a copy is we don't want alter the path

    int e = 0;
    char *err_context = "mk_dir_r";
    char *parent = malloc(strlen(path)+1);
    char *p = parent; // pointer so we can free the memory later
    int dir = 0;

    // Not the best algorithm in terms of processing time but it uses less
    // memory than making a list of all the directories we need to create.
    // The best thing would be to do it from root down. This is the opposite 
    // because I insisted on using dirname() for manipulating the path.

    for(int i=0; i<254; i++) {
        strcpy(parent, path);
        dir = open(dirname(parent),__O_DIRECTORY);
        if(dir != -1){
            // success!
            close(dir);
            free(p);
            return 0;
        }
        else {
            if (errno == ENOENT) {
                // Try to create the parent directory, then the parent's parent, 
                // etc. until successful or until an error is thrown by mk_dir.
                int j = 0; // implements timeout for safety
                int er = 0;
                strcpy(parent, path);
                do {
                    j++;
                    parent = dirname(parent);
                    //printf("mk_dir_r: dirname: %s\n", parent);
                    er = mk_dir(parent);
                    // Don't add anything here; errno musn't change.
                } while(er && errno == ENOENT && j<254);
                if(er) {
                    err_context = "mk_dir_r: mk_dir";
                    goto ErrorCleanup;
                }      
            }
            else {
                err_context = "open(parent,__O_DIRECTORY)";
                goto ErrorCleanup;
            }
        }
    }
    ErrorCleanup:
        e = errno;
        syslog(LOG_ERR,"Error in mk_dir_r...");
        errno = e;
        syslog(LOG_ERR,"%s: %m",err_context);
        syslog(LOG_DEBUG,"parent was: %s\n",parent);
        syslog(LOG_DEBUG,"path was: %s\n", path);
        free(p);
        errno = e;
        return -1;
}

/* Test function. Prints the argument strings passed to the program as comma
 * separated values. The final value has a comma on it also, for simplicity.
 * 
 * Parameters:
 *   int argc   argument count.
 *   char *argv array of char pointers. These point to the argument strings.
 */
int print_args(int argc, char *argv[]) {
    if (argc > 0) {
        for(int i = 0; i<argc; i++) {
            printf("%s, ",argv[i]);
        }
        printf("\n");
        return 0;
    }
    else
        return 1;
}
