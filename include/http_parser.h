#ifndef PC_2021A_06_HTTP_PARSER_H
#define PC_2021A_06_HTTP_PARSER_H


#include <http_chars.h>
#include <parser.h>
#include <http.h>


typedef struct parserData{
    struct parser * parser;
    buffer parseBuffer;
}parserData;

void http_parser_init(parserData * data);

parse_state http_parser_parse(buffer * readBuffer, struct request * httpRequest, parserData * data);

void http_parser_destroy(parserData * data);

#endif //PC_2021A_06_HTTP_PARSER_H
