#define _POSIX_C_SOURCE 200809L
#define __DEBUG_MESSAGES 1

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <pthread.h>

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
    bool daemonize = false;
    if(argc == 2) {
        if(strcmp(argv[1],"-d") == 0) {
            daemonize = true;
        }
    }

    int r;
    r = startserver(daemonize);  // opens syslog, socket, file
    if(r) {return r;}

    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);
    startacceptconnectionthread(&thread, &mutex, socketfiledesc, datafiledesc);
    syslog(LOG_DEBUG,"ordered start of acceptconnectionthread");
    flag_idling_main_thread = true;
    while(flag_idling_main_thread){pause();}
    syslog(LOG_INFO, "caught %s signal",strsignal(last_signal_caught));
    pthread_cancel(thread);
    r = stopserver();   // closes socket, file and syslog (and deletes file)
    return r;
}

int startserver(bool daemonize) {
    openlog("aesdsocket", LOG_CONS|LOG_PERROR|LOG_PID, LOG_USER);
    syslog(LOG_DEBUG, "creating data file %s", datapath);
    int r = createdatafile();
    if(r) {
        log_errno("main(): initializedatafile()");
        syslog(LOG_ERR, "the return value was %i", r);
        return 1;
    }

    syslog(LOG_DEBUG, "opening data file %s", datapath);
    int dfd = opendatafile();
    if( dfd == -1 ) {
        log_errno("main(): opendatafile()");
        return 1;
    }
    datafiledesc = dfd;

    syslog(LOG_DEBUG, "opening socket %s:%s", aesd_netparams.ip, aesd_netparams.port);
    int sfd = opensocket();
    if( sfd == -1 ) {
        log_errno("main(): opensocket()");
        return 1;
    }
    socketfiledesc = sfd;

    if(daemonize) {
        syslog(LOG_DEBUG,"Starting daemon");
        pid_t pid;
        pid = fork();
        if (pid < 0) {
            log_errno("main(): fork");
            return 1;
        }
        else if(pid > 0) {
            syslog(LOG_INFO,"Daemon PID: %ld",(long)pid);
            exit(EXIT_SUCCESS);
        }
    }
    
    struct sigaction signal_action;
    memset(&signal_action,0,sizeof(struct sigaction));
    signal_action.sa_handler = signal_handler;
    r  =  sigaction(SIGINT, &signal_action, NULL);
    r |= sigaction(SIGTERM, &signal_action, NULL);
    if(r) {
        log_errno("main(): sigaction()");
        return 1;
    }


    syslog(LOG_INFO, "server started");
    return 0;
}

int stopserver() {
    int r;
    syslog(LOG_DEBUG, "closing socket %s:%s", aesd_netparams.ip, aesd_netparams.port);
    r = closesocket(socketfiledesc);
    if(r) {return 1;}
    syslog(LOG_DEBUG, "closing data file %s", datapath);
    closedatafile(datafiledesc);
    if(r) {return 1;}
    syslog(LOG_DEBUG, "deleting data file %s", datapath);
    deletedatafile();
    if(r) {return 1;}
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
    robustclose(sfd);
    freeaddrinfo(bindaddresses);
    errno = errnoshadow;
    return -1;
}

int closesocket(int sfd) {
    if ( robustclose(sfd) ) {
        log_errno("closesocket(): close()");
        return -1;
    }
    return 0;
}

int opendatafile() {
    int fd = open(datapath,O_APPEND|O_CREAT|O_WRONLY|O_TRUNC);
    if(fd == -1) {
        log_errno("opendatafile(): open()");
    }
    return fd;
}

int closedatafile(int fd) {
    if(robustclose(fd)) {
        log_errno("closedatafile(): close()");
        return -1;
    }
    return 0;
}

// The proper way to use this is as a thread, because it has a blocking 
// function.
int acceptconnection(int sfd, int dfd) {
    syslog(LOG_DEBUG, "acceptconnection sfd = %i; dfd = %i",sfd,dfd);
    flag_accepting_connections = true;
    //struct filedesc_wrmutex_t *dfd_mutual;
    //dfd_mutual = (struct filedesc_t *)malloc(sizeof(struct filedesc_t));
    while(flag_accepting_connections) {
        int rsfd;   //receiving socket-file-descriptor
        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        rsfd = accept(sfd,(struct sockaddr *)&client_addr,&client_addr_len);
        if(rsfd == -1) {
            log_errno("acceptconnection(): accept()");
            return -1;
        }
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

        if(appenddata(rsfd, dfd)) {
            log_errno("acceptconnection(): appenddata()");
            if (robustclose(rsfd) == 0) {
                syslog(LOG_DEBUG,"Closed connection from %s port %s", hoststr, portstr);
                return 0;
            }
            else {
                syslog(LOG_ERR,"failed to close fd %i", rsfd);
                return -1;
            }
        }
        robustclose(rsfd);
        #ifdef __DEBUG_MESSAGES
        syslog(LOG_DEBUG,"Closed file descriptor #%i",rsfd);
        syslog(LOG_DEBUG,"Closed connection from %s port %s", hoststr, portstr);
        #endif
    }
    return 0;
}

void *acceptconnectionthread(void *thread_param) {
    struct descriptors_t *desc = (struct descriptors_t *)thread_param;
    pthread_mutex_lock(desc->mutex);
    int sfd = desc->sfd;
    int dfd = desc->dfd;
    pthread_mutex_unlock(desc->mutex);
    free(desc);
    syslog(LOG_DEBUG,"acceptconnectionthread is alive");
    acceptconnection(sfd, dfd);   //it's nice getting out of pointer-land
    return thread_param;
}

int startacceptconnectionthread(pthread_t *thread, pthread_mutex_t *mutex, int sfd, int dfd) {
    syslog(LOG_DEBUG, "startacceptconnectionthread sfd = %i; dfd = %i",sfd,dfd);
    struct descriptors_t *descriptors;
    descriptors = (struct descriptors_t *) malloc(sizeof(struct descriptors_t));
    if(descriptors == NULL) {
        perror("startacceptconnectionthread(): malloc()");
        return -1;
    }
    descriptors->mutex = mutex;
    descriptors->sfd = sfd;
    descriptors->dfd = dfd;

    // I am not going to set up a mutex for sfd_p because I only intent on 
    // accessing it on a single thread. If this ever changes, add a mutex.
    pthread_mutex_lock(descriptors->mutex); //prevent descriptors from being freed
    if(pthread_create(thread, NULL, acceptconnectionthread, descriptors)) {
        perror("startacceptconnectionthread(): pthread_create()");
        return -1;
    }
    pthread_mutex_unlock(descriptors->mutex); //now acceptconnectionthread can free it


    return 0;
}

int appenddata(int sfd, int dfd) {
    int buf_len = 10000;  //arbitrary
    void *buf = malloc(buf_len);
    size_t readcount, writecount;
    while(1) {
        readcount = read(sfd, buf, buf_len);
        if (readcount == -1) {
            log_errno("appenddata(): read()");
            free(buf);
            return -1;
        }
        else if (readcount == 0) {
            // End of file. In TCP terms, received FIN from client.
            syslog(LOG_DEBUG,"appenddata received FIN from client");
            free(buf);
            return 0;
        }
        writecount = write(dfd,buf,readcount);
        if(writecount == -1) {
            log_errno("appenddata(): write()");
            free(buf);
            return -1;
        }
        else if(writecount < readcount) {
            syslog(LOG_WARNING, "appenddata(): write(): %m ...Continuing");
            syslog(LOG_WARNING, "caused by writecount < readcount... continuing.");
        }
    }
        syslog(LOG_DEBUG,"appenddata done");
        free(buf);
        return 0;
}

int createdatafile() {
    const char cmdfmt[] = "mkdir -p $(dirname %s); touch %s";
    char cmd[PATH_MAX*2 + sizeof(cmdfmt) + 1];
    //2 chars longer than necessary for every %s
    sprintf(cmd, cmdfmt, datapath, datapath);
    return system(cmd);
}

int deletedatafile() {
    const char cmdfmt[] = "rm %s";
    char cmd[PATH_MAX + sizeof(cmdfmt)]; 
    //2 chars longer than necessary for every %s
    sprintf(cmd, cmdfmt, datapath);
    return system(cmd);
}

int robustclose(int fd) {
    if (close(fd) == 0) {
                syslog(LOG_DEBUG,"Closed file descriptor #%i", fd);
                return 0;
            }
    else{
        if(errno == EINTR) {
            int i, r;
            for(i = 0; i>5 || r == 0 || errno != EINTR; i++) {
                r = close(fd);
            }
            if (r != 0) {
                syslog(LOG_ERR,"robustclose() failed to close file descriptor %i after %i retries: %m",fd,i);
                return -1;
            } 
        }
        else {
            syslog(LOG_ERR,"robustclose() failed to close file descriptor %i",fd);
            return -1;
        }
    }
    return -1;
}

void log_errno(const char *funcname) {
    // I
    int local_errno = errno;
    syslog(LOG_ERR, "%s: %m", funcname);
    errno = local_errno;
}

void log_gai(const char *funcname, int errcode) {
    const char *errstr = gai_strerror(errcode);
    syslog(LOG_ERR, "%s: %s", funcname, errstr);
}

// TODO setup sigaction
static void signal_handler(int signo) {
    switch(signo) {
        case SIGTERM:
        case SIGINT:
            flag_accepting_connections = false;
            flag_idling_main_thread = false;
        default:
            last_signal_caught = signo;
    }
}