#  define NI_MAXHOST      1025  //these should come from netdb.h but I can't
#  define NI_MAXSERV      32    //seem to get the ifdef to work right away

#ifndef USE_AESD_CHAR_DEVICE
#   define USE_AESD_CHAR_DEVICE 0
#endif

#if USE_AESD_CHAR_DEVICE == 0
#   define AESD_DATAPATH ("/var/tmp/aesdsocketdata")
#else
#   define AESD_DATAPATH ("/dev/aesdchar")
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <netdb.h>
#include <linux/limits.h>
#include <pthread.h>

#define AESD_SOCKET_IOC_STRING "AESDCHAR_IOCSEEKTO"

struct socket_params {
    char *port;
    char *ip;
    int backlog;
    struct addrinfo ai;
};

const struct socket_params aesd_netparams  = {
    .port = "9000",
    .ip = "0.0.0.0",                         // NULL will bind to all interfaces
    .backlog = 100,
    .ai = {
        .ai_family = AF_INET,                // AF_UNSPEC for dual stack
        .ai_socktype = SOCK_STREAM,           // | SOCK_NONBLOCK,
        .ai_protocol = 0,
        .ai_flags = AI_PASSIVE,
        .ai_canonname = NULL,
        .ai_addr = NULL,
        .ai_addrlen = 0,
        .ai_next = NULL
    }
};
// To bind to a specific interface use .ip = "127.0.0.1" for example.

volatile bool flag_accepting_connections = false;
volatile bool flag_idling_main_thread = false;
volatile int  last_signal_caught = 0;

const char datapath[PATH_MAX] = AESD_DATAPATH;

struct descriptors_t {
    pthread_mutex_t *mutex;
    int dfd;                // data file descriptor
    int sfd;                // socket file descriptor
};
struct descriptors_t *server_descriptors;
pthread_t server_thread;

// Passed to appenddata() threads. This data would be on a TAILQ list and each
// list element is assigned to a thread.
struct append_t {
    pthread_mutex_t * dfdmutex;     // Mutex for data file descriptor
    int dfd;                        // Data file descriptor
    int rsfd;                       // Socket file descriptor
    pthread_t thread;               // Thread ID
    int ret;                        // Return value
    TAILQ_ENTRY(append_t) nodes;    // TAILQ nodes
};

struct timestamp_t {
    pthread_mutex_t * dfdmutex;     // Mutex for data file descriptor
    int dfd;                        // Data file descriptor
    pthread_t thread;               // Thread ID
    int ret;                        // Return value
};
struct timestamp_t *timestamp_descriptors;

int main(int argc, char *argv[]);
int startserver(bool daemonize);
int stopserver();
int opensocket();
int closesocket(int sfd);
int opendatafile();
int closedatafile(int fd);
int listenfunc(int sfd, int dfd, pthread_mutex_t *dfdmutex);
void *listenthread(void *thread_param);
int startlistenthread(pthread_t *thread, struct descriptors_t *descriptors);
void *appenddatathread(void *thread_param);
int appenddata(int sfd, int dfd, pthread_mutex_t *sfdmutex);
void *timestampthread(void *thread_param);
int timestamp(int dfd, pthread_mutex_t *dfdmutex);
int createdatafile();
int deletedatafile();
int robustclose(int fd);
void log_errno(const char *funcname);
void log_gai(const char *funcname, int errcode);
static void signal_handler(int signo);
ssize_t find_ioc_command(const void * buf, int buf_len);
struct aesd_seekto * parse_ioc_command(const void * buf, size_t startpos);
ssize_t find_eoc(const void * buf, int buf_len, size_t ioc_command_pos);
