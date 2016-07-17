#pragma once

#include "common.hpp"

typedef struct {
    worker_ctx *worker;
    int fd;
    http_parser http_req_parser;
    int keepalive;
    std::string remote_ip;
    enum {OPEN, CLOSING, CLOSED} state;
} http_connection;