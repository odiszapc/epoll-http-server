#include "http_request.hpp"

using namespace std;

int http_request_on_header_field(http_parser *parser, const char *at, size_t length) {

}

int http_request_on_header_value(http_parser *parser, const char *at, size_t length) {

}

int http_request_on_headers_complete(http_parser *parser) {

}

int http_request_on_body(http_parser *parser, const char *at, size_t length) {

}

int http_request_on_message_begin(http_parser *parser) {

}

int http_request_on_message_complete(http_parser *parser) {

}

int http_request_on_url(http_parser *parser, const char *at, size_t length) {
    std::string uri(at, length);
    fprintf(stdout, "[Web]: Request to %s\n", uri.c_str());
}
