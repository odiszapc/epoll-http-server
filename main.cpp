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
#define EPOLL_MAXEVENTS 64

using namespace std;

struct worker_ctx;

struct server_ctx {
    string host;
    string port;
    string directory;
    int workers_num;
    int socket_fd;
    vector<worker_ctx *> workers;
};

struct worker_ctx {
    server_ctx *server;
    int worker_id;
    int epoll_fd;
    int epoll_max_events;
    std::thread thread_func;
    int return_code;

};

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
 * Worker loop
 */
static void worker_func(worker_ctx *ctx) {
    struct epoll_event event;
    struct epoll_event *events;
    int ret;

    fprintf(stdout, "Worker #%d started\n", ctx->worker_id);

    // Init epoll instance
    ctx->epoll_fd = epoll_create1(0);
    if (ctx->epoll_fd == -1) {
        perror("epoll_create");
        ctx->return_code = -1;
        return;
    }

    event.data.fd = ctx->server->socket_fd;
    event.events = EPOLLIN | EPOLLET;
    ret = epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, ctx->server->socket_fd, &event);
    if (ret == -1) {
        perror("epoll_ctl");
        ctx->return_code = -1;
        return;
    }

    events = (struct epoll_event *) calloc(ctx->epoll_max_events, sizeof(event));

    /* Event loop */
    int events_received_num, i;
    while (1) {
        events_received_num = epoll_wait(ctx->epoll_fd, events, ctx->epoll_max_events, -1);
        if (-1 == events_received_num) {
            perror("epoll_wait");
            ctx->return_code = -1;
            return;
        }

        fprintf(stdout, "EPOLL: %d events received\n", events_received_num);
        for (i = 0; i < events_received_num; i++) {
            if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN))) {
                /* An error has occured on this fd, or the socket is not
                   ready for reading (why were we notified then?) */
                fprintf(stderr, "epoll error\n");
                close(events[i].data.fd);
                continue;
            }

            else if (ctx->server->socket_fd == events[i].data.fd) {
                /* Notification on server socket, which means new connection occurred */
                while (1) {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int remote_socket_fd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof in_addr;
                    remote_socket_fd = accept(ctx->server->socket_fd, &in_addr, &in_len);
                    if (remote_socket_fd == -1) {
                        if ((errno == EAGAIN) ||
                            (errno == EWOULDBLOCK)) {
                            /* We have processed all incoming connections. */
                            break;
                        }
                        else {
                            perror("accept");
                            break;
                        }
                    }

                    ret = getnameinfo(&in_addr, in_len,
                                      hbuf, sizeof hbuf,
                                      sbuf, sizeof sbuf,
                                      NI_NUMERICHOST | NI_NUMERICSERV);
                    if (0 == ret) {
                        printf("Accepted connection on descriptor %d "
                                       "(host=%s, port=%s)\n", remote_socket_fd, hbuf, sbuf);
                    }

                    /* Make incoming socket non-blocking */
                    ret = make_socket_non_blocking(remote_socket_fd);
                    if (-1 == ret) {
                        ctx->return_code = -1;
                        return;
                    }

                    /* Add incoming socket to epoll monitoring */
                    event.data.fd = remote_socket_fd;
                    event.events = EPOLLIN | EPOLLET;
                    ret = epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, remote_socket_fd, &event);
                    if (-1 == ret) {
                        perror("epoll_ctl");
                        ctx->return_code = -1;
                        return;
                    }
                }
            }

            else {
                /* Client part.
                   We have some data on fd to be read. */
                int done = 0;

                while (1) {
                    ssize_t bytes_received;
                    char buf[512];
                    bytes_received = read(events[i].data.fd, buf, sizeof buf);
                    if (bytes_received == -1) {
                        if (errno != EAGAIN) {
                            perror("read");
                            done = 1;
                        }
                        break;
                    } else if (0 == bytes_received) {
                        /* End of file */
                        done = 1;
                        break;
                    }

                    fprintf(stdout, "Received %d bytes\n", bytes_received);

                    ret = write(1, buf, bytes_received);
                    if (-1 == ret) {
                        perror("write");
                        ctx->return_code = -1;
                        return;
                    }
                }

                if (done) {
                    fprintf(stdout, "Closed connection on descriptor %d\n", events[i].data.fd);

                    /* Closing the descriptor will make epoll remove it for the monitoring set */
                    close(events[i].data.fd);
                }
            }
        }
    }

    free(events);
}

/**
 * Main func
 * /home/box/final/final -h <ip> -p <port> -d <directory>
 */
int main(int argc, char *argv[]) {
    int rez = 0;
    int sfd, s, efd;

    //server_ctx *server = (server_ctx *) malloc(sizeof(server_ctx));
    server_ctx *server = new server_ctx();

    bool host_arg = false;
    bool port_arg = false;
    bool directory_arg = false;
    server->workers_num = WORKERS_NUM;

    while ((rez = getopt(argc, argv, "h:p:d:w:")) != -1) {
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
            case 'w':
                server->workers_num = std::stoi(optarg);
                break;
        }
    }

    if (!host_arg || !port_arg || !directory_arg) {
        fprintf(stderr, "Usage: final -h <ip> -p <port> -d <directory>\n");
        return 1;
    }

    printf("host = %s, port = %s, directory = '%s', workers = %d\n",
           server->host.c_str(), server->port.c_str(), server->directory.c_str(), server->workers_num);

    // Daemonize

    // bind port
    sfd = create_and_bind(server->port, server->host);
    if (-1 == sfd) {
        return 1;
    }

    server->socket_fd = sfd;

    /* Set non-blocking mode */
    s = make_socket_non_blocking(sfd);
    if (s == -1) {
        return 1;
    }

    /* Start listening */
    s = listen(sfd, SOMAXCONN);
    if (s == -1) {
        perror("listen");
        return 1;
    }


    // start workers
    for (int i = 0; i < server->workers_num; ++i) {
        worker_ctx *worker = new worker_ctx();
        worker->server = server;
        worker->epoll_max_events = EPOLL_MAXEVENTS;
        worker->worker_id = i;
        server->workers.push_back(worker);
    }


    for (auto worker : server->workers) {
        worker->thread_func = std::thread(worker_func, worker);
    }

    // Wait for workers finish execution
    for (auto worker  : server->workers) {
        worker->thread_func.join();
        if (worker->return_code != -1) {
            fprintf(stdout, "Worker #%d finished gracefully\n", worker->worker_id);
        } else {
            fprintf(stdout, "Worker #%d crashed\n", worker->worker_id);
        }
    }


    for (auto worker : server->workers) {
        delete worker;
    }

    close (server->socket_fd);
    delete server;
    return 0;
}