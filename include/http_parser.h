#ifndef PC_2021A_06_HTTP_PARSER_H
#define PC_2021A_06_HTTP_PARSER_H


#include <mime_chars.h>
#include <http.h>


typedef struct parserData{
    int valN;
    struct parser * parser;
    char * currentMethod;
    char * currentTarget;
    char * currentHeader;
    char * currentHeaderValue;
    char ** header;
    char *** headers;
}parserData;

struct  parserData * http_request_parser_init();

void parse_http_request(uint8_t * readBuffer, struct request * httpRequest, parserData * parser ,size_t readBytes);

void destroy_parser(parserData * data);

#endif //PC_2021A_06_HTTP_PARSER_H
