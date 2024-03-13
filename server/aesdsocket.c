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

/* Comments:
 *  This server launches a thread dedicated to listening on the passive
 *  socket bound to 0.0.0.0:9000.
 *
 *  The server could be made to handle multiple connections simultaneously if
 *  appenddata() is wrapped around a thread (use the mutex to lock fdf). This
 *  is why mutex types exist around the program. In the end they weren´t
 *  necessary. As it is, the server handles connections sequentially.
 *
 *  The parent thread is free to do anything. There is a while(true) loop
 *  in main that sleeps until it gets a SIGINT or SIGTERM.
 *  
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

    

    flag_idling_main_thread = true;
    while(flag_idling_main_thread){pause();}
    syslog(LOG_INFO, "Caught signal, exiting");
    syslog(LOG_DEBUG, "Caught %s signal",strsignal(last_signal_caught));


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

    syslog(LOG_DEBUG, "opening socket %s:%s", aesd_netparams.ip, aesd_netparams.port);
    int sfd = opensocket();
    if( sfd == -1 ) {
        log_errno("main(): opensocket()");
        return 1;
    }

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

    server_descriptors = (struct descriptors_t *) malloc(sizeof(struct descriptors_t));
    server_descriptors->mutex = malloc(sizeof(pthread_mutex_t));
    server_descriptors->dfd = dfd;
    server_descriptors->sfd = sfd;
    pthread_mutex_init(server_descriptors->mutex, NULL);
    r = startacceptconnectionthread(&server_thread, server_descriptors);
    if(r) {
        log_errno("main(): startacceptconnectionthread()");
        return 1;
    }
    syslog(LOG_DEBUG,"ordered start of acceptconnectionthread");
    
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
    syslog(LOG_DEBUG, "stopping thread");
    pthread_cancel(server_thread);
    pthread_mutex_destroy(server_descriptors->mutex);
    syslog(LOG_DEBUG, "closing socket %s:%s", aesd_netparams.ip, aesd_netparams.port);
    r = closesocket(server_descriptors->sfd);
    if(r) {return 1;}
    syslog(LOG_DEBUG, "closing data file %s", datapath);
    closedatafile(server_descriptors->dfd);
    if(r) {return 1;}
    syslog(LOG_DEBUG, "deleting data file %s", datapath);
    deletedatafile();
    if(r) {return 1;}
    syslog(LOG_DEBUG, "freeing global mallocs");
    free(server_descriptors->mutex);
    free(server_descriptors);
    syslog(LOG_DEBUG, "server stopped");
    closelog();
    return 0;
}

void *acceptconnectionthread(void *thread_param) {
    struct descriptors_t *desc = (struct descriptors_t *)thread_param;
    pthread_mutex_lock(desc->mutex);
    int sfd = desc->sfd;
    int dfd = desc->dfd;
    pthread_mutex_t *dfdmutex = desc->mutex;
    pthread_mutex_unlock(desc->mutex);
    syslog(LOG_DEBUG,"acceptconnectionthread is alive");
    acceptconnection(sfd, dfd, dfdmutex);   //it's nice getting out of pointer-land, mostly
    return thread_param;
}

// The proper way to use this is as a thread, because it has a blocking 
// function.
int acceptconnection(int sfd, int dfd, pthread_mutex_t *dfdmutex) {
    int r;
    syslog(LOG_DEBUG, "acceptconnection sfd = %i; dfd = %i",sfd,dfd);
    flag_accepting_connections = true;
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
                syslog(LOG_INFO,"Incoming connection from %s port %s", hoststr, portstr);
            }
            syslog(LOG_DEBUG,"Opened new rsfd descriptor # %i",rsfd);
        #endif
        // TODO: call appenddata on an independent thread and loop
        r = appenddata(rsfd, dfd, dfdmutex);
        if(r) {
            log_errno("acceptconnection(): appenddata()");
            robustclose(rsfd);
            return r;
        }
        robustclose(rsfd);
        #ifdef __DEBUG_MESSAGES
        syslog(LOG_DEBUG,"Closed file descriptor #%i",rsfd);
        syslog(LOG_INFO,"Closed connection from %s port %s", hoststr, portstr);
        #endif
    }
    return 0;
   
}

int startacceptconnectionthread(pthread_t *thread, struct descriptors_t *descriptors) {
    int r;
    if(descriptors == NULL) {
        perror("startacceptconnectionthread(): malloc()");
        return -1;
    }
    syslog(LOG_DEBUG, "startacceptconnectionthread sfd = %i; dfd = %i",descriptors->sfd,descriptors->dfd);
    r = pthread_create(thread, NULL, acceptconnectionthread, descriptors);
    if(r) {
        perror("startacceptconnectionthread(): pthread_create()");
        return -1;
    }
    return 0;
}

int appenddata(int rsfd, int dfd, pthread_mutex_t *dfdmutex) {
    int buf_len = 1024;  //arbitrary
    void *buf = malloc(buf_len+1);
    size_t readcount, writecount;
    // Read read the socket and write into datafile
    while(true) {
        readcount = read(rsfd, buf, buf_len);
        if (readcount == -1) {
            log_errno("appenddata(): socket read()");
            goto errorcleanup;
        }
        else if (readcount == 0) {
            syslog(LOG_DEBUG,"appenddata received FIN from client");
            goto successcleanup;
        }

        writecount = write(dfd, buf, readcount);

        if(writecount == -1) {
            log_errno("appenddata(): data write()");
            goto errorcleanup;
        }
        else if(writecount < readcount) {
            syslog(LOG_ERR, "appenddata(): write(): %m");
            syslog(LOG_ERR, "caused by writecount=%li < readcount=%li", (
                    long unsigned) writecount, (long unsigned) readcount);
            goto errorcleanup;
        }
        if(readcount < buf_len) {
        // Read all of the datafile and write into the socket
            for(int pos=0,rc=-1; rc!=0; pos += rc) {
                int wc;
                rc = pread(dfd, buf, buf_len,pos);
                if (rc == -1) {
                    log_errno("appenddata(): file read()");
                    goto errorcleanup;
                }
                else if (rc != 0){
                    wc = write(rsfd,buf,rc);
                    if(wc == -1) {
                        log_errno("appenddata(): socket write()");
                        goto errorcleanup;
                    }
                    else if(wc < rc) {
                        syslog(LOG_ERR, "appenddata(): socket write(): %m");
                        syslog(LOG_ERR, "caused by writecount=%li < readcount=%li", 
                                (long unsigned)wc, (long unsigned) rc);
                        goto errorcleanup;
                    }
                }
                else if (rc == 0) {
                    syslog(LOG_DEBUG,"appenddata copied datafile to the socket");
                    break;
                }
            }
        }
    }

    successcleanup:
    free(buf);
    return 0;

    errorcleanup:
    free(buf);
    return -1;
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
    if (shutdown(sfd,2)) {
        log_errno("closesocket(): shutdown() (continuing)");
    }
    if ( robustclose(sfd) ) {
        log_errno("closesocket(): close()");
        return -1;
    }
    return 0;
}

int opendatafile() {
    int fd = open(datapath,O_APPEND|O_CREAT|O_RDWR|O_TRUNC);
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
    int local_errno = errno;
    syslog(LOG_ERR, "%s: %m", funcname);
    errno = local_errno;
}

void log_gai(const char *funcname, int errcode) {
    const char *errstr = gai_strerror(errcode);
    syslog(LOG_ERR, "%s: %s", funcname, errstr);
}

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
