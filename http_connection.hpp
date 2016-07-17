#pragma once

#include "common.hpp"

struct http_connection {
    worker_ctx *worker;
    int fd;
    http_parser http_req_parser;
    int keepalive;
    std::string remote_ip;
};