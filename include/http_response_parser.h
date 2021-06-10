#ifndef HTTP_RESPONSE_PARSER_H
#define HTTP_RESPONSE_PARSER_H

#include <http_message_parser.h>
#include <http.h>

typedef struct http_response {
    int status;
    char version[VERSION_LENGTH];
    char reason[REASON_LENGTH];

    http_message message;
} http_response;

typedef struct http_response_parser{
    struct parser * parser;
    buffer parse_buffer;
    http_response * response;
    int error_code;
    
    http_message_parser message_parser;
} http_response_parser;

void http_response_parser_init(http_response_parser * data);

parse_state http_response_parser_parse(http_response_parser * parser, buffer * read_buffer, http_response * response);

void http_response_parser_reset(http_response_parser * data);

void http_response_parser_destroy(http_response_parser * parser);

#endif
