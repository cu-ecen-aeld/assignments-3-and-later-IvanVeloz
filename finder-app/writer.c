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

#include <stdio.h>
#include <errno.h>
#include "writer.h"

int main(int argc, char *argv[]) {
    printf("Hello World\n");
    printf("Found %i arguments\n", argc);
    print_args(argc, argv);

    if(validate_args(argc))
        return 1; // failed validation
    
    char *filepath = argv[1];
    char *textstring = argv[2];

    //char *dirpath = 

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
 *     0  on success
 *    -1  on unspecified error
 *   You may want to check errno for the last system call error.
 */
int open_file(char *path) {
    return -1;
}

/* Creates the specified directory. 
 *  Parameters:
 *    char *path pointer to a string containing the path of the dir. Examples:
 *               /home/ivan/testfolder1/
 *               "/home/ivan/aesd tests/test/"
 *  Returns:
 *     0  on success
 *    -1  on unspecified error
 *    -2  when one or more parent directories do not exist
 *   You may want to check errno for the last system call error.
 */
int mk_dir() {
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
