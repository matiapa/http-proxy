#ifndef HTTP_MESSAGE_PARSER_H
#define HTTP_MESSAGE_PARSER_H

#include <parser.h>
#include <http.h>

typedef struct http_message_parser {
    struct parser * parser;
    buffer parse_buffer;
    
    int error_code;
    size_t current_body_length;
} http_message_parser;

void http_message_parser_init(http_message_parser * parser);

parse_state http_message_parser_parse(
    http_message_parser * parser, buffer * read_buffer,
    http_message * message, bool ignore_content_length
);

void http_message_parser_reset(http_message_parser * parser);

void http_message_parser_destroy(http_message_parser * parser);


#endif
