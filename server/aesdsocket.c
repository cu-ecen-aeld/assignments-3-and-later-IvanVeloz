#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>

#include "aesdsocket.h"

/* TODO:
 *  b. Opens a stream socket bound to port 9000, failing and returning -1 if 
 *     any of the socket connection steps fail.
 *  c. Listens for and accepts a connection
 *  d. Logs message to the syslog “Accepted connection from xxx” where XXXX 
 *     is the IP address of the connected client.
 *  e. Receives data over the connection and appends to file 
 *     /var/tmp/aesdsocketdata, creating this file if it doesn’t exist.
 * 
 *  Read the data line by line; don't load the entire file onto memory.
 */

int main(int argc, char *argv[]) {
    openlog("aesdsocket", LOG_CONS|LOG_PERROR|LOG_PID, LOG_USER);

    syslog(LOG_DEBUG, "server started");
    int sd = opensocket();
    if( sd == -1 ) {
        log_errno("main(): opensocket()");
        return 1;
    }

    closesocket(sd);
    syslog(LOG_DEBUG, "server stopped");
    closelog();
    return 1;
}

int opensocket() {
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if( sd == -1 ) {
        log_errno("opensocket(): socket()");
        return -1;
    }
    return sd;
}

int closesocket(int sd) {
    if ( close(sd) ) {
        log_errno("closesocket(): close()");
        return -1;
    }
    return 0;
}

void log_errno(const char *funcname) {
    int local_errno = errno;
    syslog(LOG_ERR, "%s: %m", funcname);
    errno = local_errno;
}