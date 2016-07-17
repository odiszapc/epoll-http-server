#pragma once

#include "common.hpp"

using namespace std;

void worker_func(worker_ctx *ctx);
static int data_received(worker_ctx *ctx, int remote_socket_fd, char *buf, size_t nread);