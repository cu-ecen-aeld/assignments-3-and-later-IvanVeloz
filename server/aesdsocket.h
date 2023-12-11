#  define NI_MAXHOST      1025  //these should come from netdb.h but I can't
#  define NI_MAXSERV      32    //seem to get the ifdef to work right away

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <linux/limits.h>

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

int socketfiledesc = -1;

bool flag_accepting_connections = false;

const char datapath[PATH_MAX] = "/var/tmp/aesdsocketdata";


int main(int argc, char *argv[]);
int opensocket();
int closesocket(int sfd);
int opendatafile();
int closedatafile(int fd);
int acceptconnection(int sfd);
int appenddata(int sfd);
int initializedatafile();
void log_errno(const char *funcname);
void log_gai(const char *funcname, int errcode);
void sigint_handler(int signo);