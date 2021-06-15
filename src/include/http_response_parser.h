#ifndef HTTP_RESPONSE_PARSER_H
#define HTTP_RESPONSE_PARSER_H

#include <http_message_parser.h>
#include <http.h>

typedef struct http_response_parser{
    struct parser * parser;
    http_message_parser message_parser;
    buffer parse_buffer;

    http_response response;
    int error_code;
} http_response_parser;

void http_response_parser_init(http_response_parser * data);

parse_state http_response_parser_parse(
    http_response_parser * parser, buffer * read_buffer, bool ignore_length
);

void http_response_parser_reset(http_response_parser * data);

void http_response_parser_destroy(http_response_parser * parser);

#endif
