#pragma once

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

#include "http_parser.h"

#define WORKERS_NUM 4
#define EPOLL_MAXEVENTS 64

struct worker_ctx;
struct server_ctx;

struct server_ctx {
    std::string host;
    std::string port;
    std::string directory;
    int workers_num;
    int socket_fd;
    std::vector<worker_ctx *> workers;
    http_parser_settings *parser_settings;
};


struct worker_ctx {
    server_ctx *server;
    int worker_id;
    int epoll_fd;
    int epoll_max_events;
    std::thread thread_func;
    int return_code;
};


/**
 * Set socket to Non-blocking mode
 */
static inline int make_socket_non_blocking(int sfd) {
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