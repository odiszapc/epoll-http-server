#pragma once

#include "common.hpp"
#include "server.hpp"
#include "http_connection.hpp"

using namespace std;

struct worker_ctx {
    server_ctx *server;
    int worker_id;
    int epoll_fd;
    int epoll_max_events;
    std::thread thread_func;
    int return_code;
    uint32_t connections_num;
    unordered_map<int, http_connection*> connection_map;
};

void worker_func(worker_ctx *ctx);
static int data_received(worker_ctx *ctx, int remote_socket_fd, char *buf, size_t nread);
static int on_new_connection(worker_ctx *worker, int remote_socket_fd, char string1[]);
static http_connection *find_connection(worker_ctx *ctx, int remote_socket_fd);
static void on_disconnect(worker_ctx *worker, int remote_socket_fd);