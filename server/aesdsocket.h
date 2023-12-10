struct socket_params {
    int server_port;
    char *server_addr;

};

const struct socket_params aesdsocket_params  = {
    .server_port = 9000,
    .server_addr = "localhost",
};

int main(int argc, char *argv[]);
int opensocket();
int closesocket(int sd);
void log_errno(const char *funcname);