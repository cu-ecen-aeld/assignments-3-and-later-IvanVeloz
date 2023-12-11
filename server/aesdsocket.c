#define _POSIX_C_SOURCE 200809L
#define __DEBUG_MESSAGES 1

#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
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
    int sfd = opensocket();
    if( sfd == -1 ) {
        log_errno("main(): opensocket()");
        return 1;
    }
    socketfiledesc = sfd;
    // start SIGINT and SIGTERM signal handlers here
    acceptconnection(socketfiledesc);  // should be on its own thread
    closesocket(socketfiledesc);
    syslog(LOG_DEBUG, "server stopped");
    closelog();
    return 0;
}

int opensocket() {

    int errnoshadow;

    struct addrinfo *bindaddresses, *rp;
    int sfd = -1;    //socket file descriptor
    int r = getaddrinfo(aesd_netparams.ip, aesd_netparams.port,
                        &(aesd_netparams.ai),&bindaddresses);
    if (r) {
        log_gai("opensocket(): getaddrinfo()", r);
        return -1;
    }

    // This for loop comes from the example at the getaddrinfo(3) manpage,
    // on the Linux Programmer's Manual. Pretty interesting for loop honestly.
    // This loop will iterate over the bindaddresses until one is found that
    // can be used to open a socket. In my testing only one address is returned
    // usually.
    for(rp = bindaddresses; rp != NULL; rp = rp->ai_next) {
        sfd = socket(   aesd_netparams.ai.ai_family,
                        aesd_netparams.ai.ai_socktype,
                        aesd_netparams.ai.ai_protocol);
        if( sfd == -1 ) {
            syslog(LOG_DEBUG, "opensocket(): socket() loop: %m");
            continue;
        }
        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        else {
            log_errno("opensocket(): bind()");
            goto errorcleanup;
        }

    }
    if (rp == NULL) {
        syslog(LOG_ERR, "opensocket(): could not bind to any address");
        goto errorcleanup;
    }
    // the following type cast comes from https://beej.us/guide/bgnet/html/
    void *addr = &(((struct sockaddr_in *)rp->ai_addr)->sin_addr);
    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(rp->ai_family, addr, ipstr, sizeof(ipstr));
    syslog(LOG_INFO, "Bound to %s", ipstr);
    if(listen(sfd, aesd_netparams.backlog)) {
        log_errno("opensocket(): listen()");
        goto errorcleanup;
    }
    freeaddrinfo(bindaddresses);
    return sfd;

    errorcleanup:
    errnoshadow = errno;
    close(sfd);
    freeaddrinfo(bindaddresses);
    errno = errnoshadow;
    return -1;
}

int closesocket(int sfd) {
    if ( close(sfd) ) {
        log_errno("closesocket(): close()");
        return -1;
    }
    return 0;
}

// The proper way to use this is as a thread, because it has a blocking 
// function.
int acceptconnection(int sfd) {
    flag_accepting_connections = true;
    while(flag_accepting_connections) {
        int rsfd;   //receiving socket-file-descriptor
        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        rsfd = accept(sfd,(struct sockaddr *)&client_addr,&client_addr_len);

        #ifdef __DEBUG_MESSAGES
            char hoststr[NI_MAXHOST];
            char portstr[NI_MAXSERV];
            if (getnameinfo((struct sockaddr *)&client_addr, client_addr_len,
                            hoststr, sizeof(hoststr), portstr, sizeof(portstr),
                            NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
                syslog(LOG_DEBUG,"Incoming connection from %s port %s", hoststr, portstr);
            }
            syslog(LOG_DEBUG,"Opened new descriptor # %i",rsfd);
        #endif
        
    }
    return 0;
}

void log_errno(const char *funcname) {
    int local_errno = errno;
    syslog(LOG_ERR, "%s: %m", funcname);
    errno = local_errno;
}

void log_gai(const char *funcname, int errcode) {
    const char *errstr = gai_strerror(errcode);
    syslog(LOG_ERR, "%s: %s", funcname, errstr);
}

// TODO setup sigaction
void sigint_handler(int signo) {
    switch(signo) {
        case SIGINT:
            if(flag_accepting_connections)
                flag_accepting_connections = false;
    }
}