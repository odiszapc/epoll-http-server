#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>

#include <iostream>
#include <string>
#include <thread>
#include <vector>

#define WORKERS_NUM 4

using namespace std;

struct worker_ctx;

struct server_ctx {
    string host;
    string port;
    string directory;
    int workers_num;
    int socket_fd;
    vector<worker_ctx*> workers;

//    server_ctx():host(), port(), directory(), workers() {
//
//    }
};

struct worker_ctx {
    server_ctx *server;
    int id;
    int epoll_fd;
};

void worker_func() {

}

int make_socket_non_blocking(int sfd) {
    int flags, s;

    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if (s == -1) {
        perror("fcntl");
        return -1;
    }

    return 0;
}

/**
 * Bind socket
 */
static int create_and_bind(const string &port, const string &ip) {
    int r;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
    hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
    hints.ai_flags = INADDR_ANY;     /* All interfaces */

    s = getaddrinfo(ip.c_str(), port.c_str(), &hints, &result);
    if (0 != s) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (-1 == sfd)
            continue;

        s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
        if (0 == s) {
            /* Bind is successful */
            break;
        }
        close(sfd);
    }

    if (NULL == rp) {
        fprintf(stderr, "Could not bind\n");
        return -1;
    }

    freeaddrinfo(result);

    return sfd;
}


/**
 * /home/box/final/final -h <ip> -p <port> -d <directory>
 */
int main(int argc, char *argv[]) {
    int rez = 0;
    int sfd, s, efd;

    //server_ctx *server = (server_ctx *) malloc(sizeof(server_ctx));
    server_ctx *server = new server_ctx();

    //std::string host;
    bool host_arg = false;
    //std::string port;
    bool port_arg = false;
    //std::string directory;
    bool directory_arg = false;

    while ((rez = getopt(argc, argv, "h:p:d:")) != -1) {
        switch (rez) {
            case 'h':
                server->host = std::string(optarg);
                host_arg = true;
                break;
            case 'p':
                server->port = std::string(optarg);
                port_arg = true;
                break;
            case 'd':
                server->directory = std::string(optarg);
                directory_arg = true;
                break;
        }
    }

    if (!host_arg || !port_arg || !directory_arg) {
        fprintf(stderr, "Usage: final -h <ip> -p <port> -d <directory>\n");
        abort();
    }

    printf("host = %s, port = %s, directory = '%s'\n",
           server->host.c_str(), server->port.c_str(), server->directory.c_str());

    // Daemonize

    // bind port
    sfd = create_and_bind(server->port, server->host);
    if (-1 == sfd) {
        abort();
    }

    server->socket_fd = sfd;

    /* Set non-blocking mode */
    s = make_socket_non_blocking(sfd);
    if (s == -1) {
        abort();
    }

    /* Start listening */
    s = listen(sfd, SOMAXCONN);
    if (s == -1) {
        perror("listen");
        abort();
    }


    server->workers_num = WORKERS_NUM;
    // start workers
    for (int i = 0; i < server->workers_num; ++i) {
        worker_ctx *worker = new worker_ctx();
        worker->server = server;

        efd = epoll_create1(0);
        if (efd == -1) {
            perror("epoll_create");
            abort();
        }
        worker->epoll_fd = efd;
        worker->id = i;
        printf("worker #%d ready\n", worker->id);
        server->workers.push_back(worker);
        //thread
    }


    sleep(1000000);
    delete server;
    return 0;
}