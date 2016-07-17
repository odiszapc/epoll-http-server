#include "worker.hpp"
#include "http_connection.hpp"
#include "http_request.hpp"

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
                        printf("[]: Accepted new connection on descriptor %d "
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

                    fprintf(stdout, "[]: Received %d bytes\n\n", bytes_received);
                    done = data_received(ctx, events[i].data.fd, buf, bytes_received);
                }

                if (done || find_connection(ctx, events[i].data.fd)->state == http_connection::CLOSING) {
                    fprintf(stdout, "[]: Closed connection on descriptor %d\n\n", events[i].data.fd);

                    ret = epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, &event);
                    /* Closing the descriptor will make epoll remove it for the monitoring set */
                    close(events[i].data.fd);
                    on_disconnect(ctx, events[i].data.fd);
                }
            }
        }
    }

    free(events);
}

static http_parser _parser;

static int on_new_connection(worker_ctx *worker, int remote_socket_fd, char *ip) {
    http_connection *conn = new http_connection();
    conn->worker = worker;
    conn->fd = remote_socket_fd;
    conn->keepalive = 0;
    conn->remote_ip = std::string(ip);
    conn->state = http_connection::OPEN;

    http_parser_init(&conn->http_req_parser, HTTP_REQUEST);
    http_parser_init(&_parser, HTTP_REQUEST);

    // Link to connection
    conn->http_req_parser.data = conn;

    worker->connections_num += 1;
    //worker->connection_map.insert(std::make_pair<int, http_connection*>(remote_socket_fd, conn));
    worker->connection_map.insert({{remote_socket_fd, conn}});

    fprintf(stdout, "[]: New connection from %s\n\n", conn->remote_ip.c_str());
}

static void on_disconnect(worker_ctx *worker, int remote_socket_fd) {
    const http_connection *conn = find_connection(worker, remote_socket_fd);
    if (NULL == conn) {
        return;
    }

    worker->connection_map.erase(remote_socket_fd);

    fprintf(stdout, "[]: Client '%s' disconnected\n\n", conn->remote_ip.c_str());
}

static int data_received(worker_ctx *worker, int remote_socket_fd, char *buf, size_t nread) {
    //http_parser *parser = (http_parser *) malloc(sizeof(http_parser));
    //http_parser_init(parser, HTTP_REQUEST);

    const http_connection *conn = find_connection(worker, remote_socket_fd);
    if (NULL == conn) {
        return 1;
    }

    fprintf(stdout, "[]: Read %d bytes from %s\n\n", nread, conn->remote_ip.c_str());

    http_parser_execute((http_parser *) &conn->http_req_parser, &worker->server->parser_settings, (const char *) buf,nread);

    int ret = write(1, (const char *) buf, nread);




    return 1; // 1 means close connection
}

static http_connection *find_connection(worker_ctx *worker, int remote_socket_fd) {
    auto it = worker->connection_map.find(remote_socket_fd);
    if (it == worker->connection_map.end()) {
        fprintf(stderr, "[]: Unknown fd: %d\n\n", remote_socket_fd);
        return NULL;
    }
    return it->second;
}