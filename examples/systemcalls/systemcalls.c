#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <fcntl.h>
#include "systemcalls.h"

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    return (system(cmd) == 0);
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

/*
 * DONE:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
 */
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();

    if(pid == -1) {
        perror("do_exec: fork() failed");
        va_end(args);
        return false;
    }
    else if(pid == 0) {
        // This process is the child process
        printf("do_exec: child process: command path is %s\n", command[0]);
        for(int i = 0; i<count; i++) {
            printf("do_exec: child process: command arg %i is %s\n",i,command[i]);
        }
        int ret;
        ret = execv(command[0],command);
        perror("do_exec: child process: execv failed");
        exit(ret); // terminate the child with ret as the return value
    }

    int childstatus;
    pid_t r = waitpid(pid, &childstatus,0);
    if(r == -1){
        perror("do_exec: wait() failed");
        va_end(args);
        return false;
    }

    if(WIFEXITED(childstatus) == false) {
        fprintf(stderr,"do_exec: WIFEXITED(): child didn't exit normally, wstatus is %i\n", childstatus);
        va_end(args);
        return false;
    }

    int exitstatus = WEXITSTATUS(childstatus);
    if(exitstatus != 0) {
        fprintf(stderr,"do_exec: WEXITSTATUS(): child exited with status %i\n", exitstatus);
        va_end(args);
        return false;
    }

    printf("do_exec: status was %i\n", exitstatus);
    va_end(args);
    return true;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];


/*
 * DONE
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
    int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT);
    if (fd == -1) {
        perror("do_exec_redirect: open");
        va_end(args);
        return false;
    }

    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();

    if(pid == -1) {
        perror("do_exec_redirect: fork() failed");
        va_end(args);
        return false;
    }
    else if(pid == 0) {
        // This process is the child process
        if(dup2(fd,1) == -1) {
            perror("do_exec_redirect: child: dup2");
            exit(1);
        }
        close(fd);
        /*
        printf("do_exec_redirect: child process: command path is %s\n", command[0]);
        for(int i = 0; i<count; i++) {
            printf("do_exec: child process: command arg %i is %s\n",i,command[i]);
        }
        */
        int ret;
        ret = execv(command[0],command);
        perror("do_exec_redirect: child process: execv failed");
        exit(ret); // terminate the child with ret as the return value
    }

    close(fd);
    int childstatus;
    pid_t r = waitpid(pid, &childstatus,0);
    if(r == -1){
        perror("do_exec_redirect: wait() failed");
        va_end(args);
        return false;
    }

    if(WIFEXITED(childstatus) == false) {
        fprintf(stderr,"do_exec_redirect: WIFEXITED(): child didn't exit normally, wstatus is %i\n", childstatus);
        va_end(args);
        return false;
    }

    int exitstatus = WEXITSTATUS(childstatus);
    if(exitstatus != 0) {
        fprintf(stderr,"do_exec_redirect: WEXITSTATUS(): child exited with status %i\n", exitstatus);
        va_end(args);
        return false;
    }

    printf("do_exec_redirect: status was %i\n", exitstatus);
    va_end(args);
    return true;

}
