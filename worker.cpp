#include "worker.hpp"
#include "http_connection.hpp"

/**
 * Worker loop
 */
void worker_func(worker_ctx *ctx) {
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

        //fprintf(stdout, "EPOLL: %d events received\n", events_received_num);
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
                        printf("[]: Accepted connection on descriptor %d "
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

                    on_new_connection(ctx, remote_socket_fd, hbuf);
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

                    fprintf(stdout, "[]: Received %d bytes\n", bytes_received);
                    done = data_received(ctx, events[i].data.fd, buf, bytes_received);

                    ret = write(1, buf, bytes_received);
                    if (-1 == ret) {
                        perror("write");
                        ctx->return_code = -1;
                        return;
                    }
                }

                if (done) {
                    fprintf(stdout, "[]: Closed connection on descriptor %d\n", events[i].data.fd);

                    /* Closing the descriptor will make epoll remove it for the monitoring set */
                    close(events[i].data.fd);
                }
            }
        }
    }

    free(events);
}

static int on_new_connection(worker_ctx *worker, int remote_socket_fd, char* ip) {
    http_connection *conn = new http_connection();
    conn->worker = worker;
    conn->fd = remote_socket_fd;
    conn->keepalive = 0;
    conn->remote_ip = std::string(ip);
    http_parser *parser = (http_parser *) malloc(sizeof(http_parser));
    http_parser_init(parser, HTTP_REQUEST);
    conn->http_req_parser = parser;

    // Link to connection
    parser->data = conn;

    worker->connections_num += 1;
    //worker->connection_map.insert(std::make_pair<int, http_connection*>(remote_socket_fd, conn));
    worker->connection_map.insert({{remote_socket_fd, conn}});

    fprintf(stdout, "New connection from %s\n", conn->remote_ip.c_str());
}

static int data_received(worker_ctx *ctx, int remote_socket_fd, char *buf, size_t nread) {
    http_parser *parser = (http_parser *) malloc(sizeof(http_parser));
    http_parser_init(parser, HTTP_REQUEST);

    auto it = ctx->connection_map.find(remote_socket_fd);
    if (it == ctx->connection_map.end()) {
        fprintf(stderr, "Unknown fd: %d\n", remote_socket_fd);
    }

    http_connection *conn = it->second;

    http_parser_execute(parser, ctx->server->parser_settings, (const char *) buf, nread);


    return 1; // 1 means close connection
}