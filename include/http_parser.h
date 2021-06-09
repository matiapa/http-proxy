#ifndef PC_2021A_06_HTTP_PARSER_H
#define PC_2021A_06_HTTP_PARSER_H


#include <http_chars.h>
#include <parser.h>
#include <http.h>


typedef struct parser_data{
    struct parser * parser;
    struct buffer parse_buffer;
    struct request * request;
}parser_data;

void http_parser_init(parser_data * data);

parse_state http_parser_parse(buffer * readBuffer, struct request * httpRequest, parser_data * data);

void http_parser_destroy(parser_data * data);

#endif //PC_2021A_06_HTTP_PARSER_H
