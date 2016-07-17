#include "common.hpp"

#include "http_parser.h"
#include "http_request.hpp"
#include "worker.hpp"
#include "server.hpp"

using namespace std;



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

        /* set SO_REUSEPORT */
        int enable = 1;
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &enable, sizeof(int)) < 0) {
            fprintf(stderr, "setsockopt(SO_REUSEADDR | SO_REUSEPORT) failed: %s\n", strerror(errno));
            return -1;
        }

        s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
        if (0 == s) {
            /* Bind is successful */
            break;
        }
        close(sfd);
    }

    if (NULL == rp) {
        fprintf(stderr, "Could not bind: %s\n", strerror(errno));
        return -1;
    }

    freeaddrinfo(result);

    return sfd;
}




/**
 * Main func
 * /home/box/final/final -h <ip> -p <port> -d <directory>
 */
int main(int argc, char *argv[]) {
    int rez = 0;
    int server_fd, s, efd;

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
    server_fd = create_and_bind(server->port, server->host);
    if (-1 == server_fd) {
        return 1;
    }

    server->socket_fd = server_fd;

    /* Set non-blocking mode */
    s = make_socket_non_blocking(server_fd);
    if (s == -1) {
        return 1;
    }

    /* Setup parser callbacks */
    server->parser_settings->on_header_field = http_request_on_header_field;
    server->parser_settings->on_header_value = http_request_on_header_value;
    server->parser_settings->on_headers_complete = http_request_on_headers_complete;
    server->parser_settings->on_body = http_request_on_body;
    server->parser_settings->on_message_begin = http_request_on_message_begin;
    server->parser_settings->on_message_complete = http_request_on_message_complete;
    server->parser_settings->on_url = http_request_on_url;

    /* Start listening */
    s = listen(server_fd, SOMAXCONN);
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

    close(server->socket_fd);
    delete server;
    return 0;
}