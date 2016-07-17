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
#include <map>
#include <unordered_map>

#include "http_parser.h"

#define WORKERS_NUM 4
#define EPOLL_MAXEVENTS 64

struct worker_ctx;
struct server_ctx;
static http_parser_settings parser_settings;

#define SETSTRING(s,val) s.value=val; s.length=STRLENOF(val)
#define APPENDSTRING(s,val) memcpy((char*)s->value + s->length, val, STRLENOF(val)); s->length+=STRLENOF(val)


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

inline bool file_exists (const std::string& name) {
    struct stat buffer;
    return (stat (name.c_str(), &buffer) == 0);
}