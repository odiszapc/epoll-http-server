#include "http_request.hpp"
#include "http_connection.hpp"
#include "worker.hpp"

using namespace std;

int http_request_on_header_field(http_parser *parser, const char *at, size_t length) {
    std::string s(at, length);
//    fprintf(stdout, "[Web]: http_request_on_header_field: '%s'\n", s.c_str());
}

int http_request_on_header_value(http_parser *parser, const char *at, size_t length) {
    std::string s(at, length);
//    fprintf(stdout, "[Web]: http_request_on_header_value: '%s'\n", s.c_str());
}

int http_request_on_headers_complete(http_parser *parser) {
//    fprintf(stdout, "[Web]: http_request_on_headers_complete\n");
}

int http_request_on_body(http_parser *parser, const char *at, size_t length) {
//    fprintf(stdout, "[Web]: http_request_on_body\n");

}

int http_request_on_message_begin(http_parser *parser) {
//    fprintf(stdout, "[Web]: http_request_on_message_begin\n");

}

int http_request_on_message_complete(http_parser *parser) {
//    fprintf(stdout, "[Web]: http_request_on_message_complete\n");

}

int http_request_on_url(http_parser *parser, const char *at, size_t length) {
    std::string uri(at, length);
    fprintf(stdout, "[Web]: Request to '%s'\n", uri.c_str());

    http_connection *conn = (http_connection *) parser->data;

    // try to find file


    std::string &dir = conn->worker->server->directory;

    if (0 == dir.compare(dir.length() - 1, 1, "/")) {
        printf("[]: truncate\n");
        dir.erase(dir.length() - 1, dir.length());
    }

    size_t index = uri.find('?',0);
    if (index != string::npos) {
        uri = uri.substr(0, index);
    }


    std::string path = dir + uri;

    printf("Full path: '%s'\n", path.c_str());

    // Check if file exists
    int ret;
    if (file_exists(path)) {
        #define HTTP_OK "HTTP/1.1 200 OK\r\n"
        #define CONTENT_TYPE "Content-Type: text/html\r\n"
        #define CONNECTION_CLOSE "Connection: close\r\n\r\n"

        ret = send(conn->fd, HTTP_OK, strlen(HTTP_OK), 0);
        if (EAGAIN == ret) {
            printf("[]: EAGAIN\n");
        }

        ret = send(conn->fd, CONTENT_TYPE, strlen(CONTENT_TYPE), 0);
        if (EAGAIN == ret) {
            printf("[]: EAGAIN\n");
        }

        ret = send(conn->fd, CONNECTION_CLOSE, strlen(CONNECTION_CLOSE), 0);
        if (EAGAIN == ret) {
            printf("[]: EAGAIN\n");
        }


        //send(conn->fd, "ABCDEFG\n123", 11, 0);

        FILE *fin;
        if ((fin = fopen(path.c_str(), "r")) == NULL) {
            perror("!!! Fail to open file");
            conn->state = http_connection::CLOSING;
            return 0;
        }
        char *buf = (char *) malloc(512);
        while (1) {
            size_t nread = fread(buf, 1, 50, fin);
            printf("[FILE]: Read %d bytes\n", nread);
            if (0 == nread) {
                break;
            }

            size_t sent = send(conn->fd, buf, nread, 0);
            if (EAGAIN == sent) {
                printf("[]: EAGAIN\n");
            }

            //usleep(10000);
            printf("[FILE]: Sent %d bytes\n", sent);

        }
        fclose(fin);
        free(buf);

    } else {
        send(conn->fd, "HTTP/1.1 404 Not Found\r\n", 26, 0);
        send(conn->fd, "Connection: close\r\n\r\n", 21, 0);
    }


    conn->state = http_connection::CLOSING;
}
