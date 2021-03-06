#ifndef HTTP_REQUEST_PARSER_H
#define HTTP_REQUEST_PARSER_H

#include <http_message_parser.h>
#include <http.h>

typedef struct http_request_parser{
    struct parser * parser;
    http_message_parser message_parser;
    buffer parse_buffer;

    http_request request;
    int error_code;
} http_request_parser;

void http_request_parser_init(http_request_parser * data);

parse_state http_request_parser_parse(
    http_request_parser * parser, buffer * read_buffer
);

void http_request_parser_reset(http_request_parser * data);

void http_request_parser_destroy(http_request_parser * parser);

#endif
