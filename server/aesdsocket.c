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
#include <poll.h>
#include <pthread.h>
#include <sys/queue.h>

#include "aesdsocket.h"

#define POLL_TIMEOUT_MS     20
#define TIMESTAMP_MAX_SIZE  100
#define TIMESTAMP_FMT       "timestamp:%a, %d %b %Y %T %z\n"

/* Comments:
 *  This server launches a thread dedicated to listening on the passive
 *  socket bound to 0.0.0.0:9000.
 *
 *  After an incoming connection is established on that socket, a receiving
 *  socket is open to communicate with the client. A new thread is created
 *  to handle the communication with that particular client.
 * 
 *  A global mutex makes sure the log file is only writen to by one thread at
 *  a time.
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

    struct sigaction signal_action;
    memset(&signal_action,0,sizeof(struct sigaction));
    signal_action.sa_handler = signal_handler;
    r  =  sigaction(SIGINT, &signal_action, NULL);
    r |= sigaction(SIGTERM, &signal_action, NULL);
    if(r) {
        log_errno("main(): sigaction()");
        return 1;
    }

    server_descriptors = 
        (struct descriptors_t *) malloc(sizeof(struct descriptors_t));
    server_descriptors->mutex = malloc(sizeof(pthread_mutex_t));
    server_descriptors->dfd = dfd;
    server_descriptors->sfd = sfd;
    pthread_mutex_init(server_descriptors->mutex, PTHREAD_MUTEX_NORMAL);

    timestamp_descriptors = 
        (struct timestamp_t *) malloc(sizeof(struct timestamp_t));
    timestamp_descriptors->dfd = dfd;
    timestamp_descriptors->dfdmutex = server_descriptors->mutex;

    syslog(LOG_DEBUG,"Ordering start timestamp");
    r = pthread_create(&timestamp_descriptors->thread, NULL, timestampthread, timestamp_descriptors);
    if(r) {
        perror("startlistenthread(): pthread_create()");
        return -1;
    }

    syslog(LOG_DEBUG,"Ordering start of listenfunc");

    r = listenfunc( server_descriptors->sfd, 
                    server_descriptors->dfd,
                    server_descriptors->mutex);
    if(r) {
        log_errno("main(): startlistenthread()");
        return 1;
    }
    return 0;
}

int stopserver() {
    int r;
    syslog(LOG_DEBUG, "sending SIGTERM to timestampthread");
    pthread_kill(timestamp_descriptors->thread, SIGINT);
    syslog(LOG_DEBUG, "waiting for timestamp thread to exit");
    r = pthread_join(timestamp_descriptors->thread, NULL);
    pthread_mutex_destroy(server_descriptors->mutex);
    syslog(LOG_DEBUG, "closing socket %s:%s", aesd_netparams.ip, aesd_netparams.port);
    r = closesocket(server_descriptors->sfd);
    if(r) {return 1;}
    syslog(LOG_DEBUG, "closing data file %s", datapath);
    r = closedatafile(server_descriptors->dfd);
    if(r) {return 1;}
    syslog(LOG_DEBUG, "deleting data file %s", datapath);
    r = deletedatafile();
    if(r) {return 1;}
    syslog(LOG_DEBUG, "freeing global mallocs");
    pthread_mutex_destroy(server_descriptors->mutex);
    free(server_descriptors->mutex);
    free(server_descriptors);
    syslog(LOG_DEBUG, "server stopped");
    closelog();
    return 0;
}

void *listenthread(void *thread_param) {
    struct descriptors_t *desc = (struct descriptors_t *)thread_param;
    int sfd = desc->sfd;
    int dfd = desc->dfd;
    pthread_mutex_t *dfdmutex = desc->mutex;
    syslog(LOG_DEBUG,"listenthread is alive");
    listenfunc(sfd, dfd, dfdmutex);   //it's nice getting out of pointer-land, mostly
    return thread_param;
}

int listenfunc(int sfd, int dfd, pthread_mutex_t *dfdmutex) {
    int r;
    syslog(LOG_DEBUG, "acceptconnection sfd = %i; dfd = %i",sfd,dfd);
    flag_accepting_connections = true;
    int rsfd;   //receiving socket-file-descriptor
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    TAILQ_HEAD(head_s, append_t) append_head;
    TAILQ_INIT(&append_head);
    struct append_t * append_inst = NULL;

    struct pollfd sfd_poll = { 
        .fd = sfd, 
        .events  = POLLIN|POLLPRI, 
        .revents = 0
    };

    while(flag_accepting_connections) {

        poll(&sfd_poll,1,POLL_TIMEOUT_MS);
        if(!(sfd_poll.revents&(POLLIN|POLLPRI)))
            continue;

        rsfd = accept(sfd,(struct sockaddr *)&client_addr,&client_addr_len);
        if(rsfd == -1) {
            log_errno("listenfunc(): accept()");
            goto errorcleanup;
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

        append_inst = NULL;
        append_inst = malloc(sizeof(struct append_t));
        if(append_inst == NULL) {
            log_errno("listenfunc(): malloc()");
            goto errorcleanup;
        }

        append_inst->rsfd = rsfd;
        append_inst->dfd = dfd;
        append_inst->dfdmutex = dfdmutex;
        r = pthread_create(&append_inst->thread, NULL, appenddatathread, append_inst);
        TAILQ_INSERT_TAIL(&append_head,append_inst,nodes);

        if(r) {
            log_errno("listenfunc(): appenddata()");
            robustclose(rsfd);
            goto errorcleanup;
        }
    }

    r = 0;
    errorcleanup:
    TAILQ_FOREACH(append_inst, &append_head, nodes) {
        pthread_join(append_inst->thread,NULL);
    }
    while(!TAILQ_EMPTY(&append_head))
    {
        append_inst = TAILQ_FIRST(&append_head);
        TAILQ_REMOVE(&append_head, append_inst, nodes);
        free(append_inst);
        append_inst = NULL;
    }
    return r;
}

int startlistenthread(pthread_t *thread, struct descriptors_t *descriptors) {
    int r;
    if(descriptors == NULL) {
        perror("startlistenthread(): malloc()");
        return -1;
    }
    syslog(LOG_DEBUG, "startlistenthread sfd = %i; dfd = %i",descriptors->sfd,descriptors->dfd);
    r = pthread_create(thread, NULL, listenthread, descriptors);
    if(r) {
        perror("startlistenthread(): pthread_create()");
        return -1;
    }
    return 0;
}

void *appenddatathread(void *thread_param) {
    struct append_t  * d = (struct append_t *)thread_param;
    d->ret = appenddata(d->rsfd, d->dfd, d->dfdmutex);
    closesocket(d->rsfd);
    #ifdef __DEBUG_MESSAGES
    syslog(LOG_DEBUG,"Closed file descriptor #%i",d->rsfd);
    #endif
    return thread_param;
}

int appenddata(int rsfd, int dfd, pthread_mutex_t *dfdmutex) {

    int r = -1;
    int buf_len = 1024;  //arbitrary
    void *buf = malloc(buf_len+1);
    size_t readcount, writecount;
    struct pollfd rsfd_poll = { 
        .fd = rsfd, 
        .events  = POLLIN|POLLPRI, 
        .revents = 0
    };
    struct timespec timeout;
    // Read the socket and write into datafile

    while(flag_accepting_connections) {

        poll(&rsfd_poll,1,POLL_TIMEOUT_MS);
        if(!(rsfd_poll.revents&(POLLIN|POLLPRI)))
            continue;

        readcount = read(rsfd, buf, buf_len);
        if (readcount == -1) {
            log_errno("appenddata(): socket read()");
            goto errorcleanup;
        }
        else if (readcount == 0) {
            syslog(LOG_DEBUG,"appenddata received FIN from client");
            break;
        }

        do {
            clock_gettime(CLOCK_REALTIME,&timeout);
            timeout.tv_nsec += (POLL_TIMEOUT_MS*1000);
        } while(pthread_mutex_timedlock(dfdmutex,&timeout));
        writecount = write(dfd, buf, readcount);
        pthread_mutex_unlock(dfdmutex);

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

    r = 0;
    errorcleanup:
    free(buf);
    return r;
}

void *timestampthread(void *thread_param) {
    int r;
    struct timestamp_t * d = (struct timestamp_t *) thread_param;
    struct timespec period = {.tv_sec = 10, .tv_nsec = 0};
    do
    {
        r = clock_nanosleep(CLOCK_MONOTONIC,0,&period,NULL);
        if(r == EINTR) {
            r = 0;
            break;
        }
        else if(r) {
            log_errno("timestampthread(): clock_nanosleep()");
            goto errorcleanup;
        }
        d->ret = timestamp(d->dfd, d->dfdmutex);
    } while(flag_accepting_connections);
    r = 0;
    errorcleanup:
    d->ret = r;
    return thread_param;
}

int timestamp(int dfd, pthread_mutex_t *dfdmutex) {
    int r = -1;
    char tstr[TIMESTAMP_MAX_SIZE];
    size_t tstr_size;
    time_t t;
    struct timespec timeout;
    ssize_t writecount;

    do {
        clock_gettime(CLOCK_REALTIME,&timeout);
        timeout.tv_nsec += (POLL_TIMEOUT_MS*1000);
    } while(pthread_mutex_timedlock(dfdmutex,&timeout));

    t = time(NULL);

    if(t == (time_t)-1) {
        log_errno("timestamp(): ");
        goto errorcleanup;
    }

    struct tm * tm = gmtime(&t);

    tstr_size = strftime(tstr, TIMESTAMP_MAX_SIZE, TIMESTAMP_FMT, tm);
    if(tstr_size == 0) {
        syslog(LOG_DEBUG,"strftime string size is too small");
        goto errorcleanup;
    }

    writecount = write(dfd, tstr, tstr_size);
    if(writecount < tstr_size) {
        log_errno("timestamp(): write(): ");
        goto errorcleanup;
    }

    r = 0;
    syslog(LOG_DEBUG,"Appended %s",tstr);
    errorcleanup:
    pthread_mutex_unlock(dfdmutex);
    return r;
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
    int fd = open(datapath, O_APPEND|O_CREAT|O_RDWR|O_TRUNC, S_IRWXU|S_IRWXG);
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
