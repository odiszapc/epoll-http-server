#pragma once

#include "common.hpp"

using namespace std;

struct server_ctx {
    std::string host;
    std::string port;
    std::string directory;
    int workers_num;
    int socket_fd;
    std::vector<worker_ctx *> workers;
    http_parser_settings parser_settings;
};