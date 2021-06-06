#ifndef PC_2021A_06_HTTP_PARSER_H
#define PC_2021A_06_HTTP_PARSER_H

#include<stdio.h>
#include <stdlib.h>  // malloc
#include <string.h>  // memset
#include <assert.h>  // assert
#include <errno.h>
#include <time.h>
#include <unistd.h>  // close
#include <pthread.h>

#include <http_parser.h>
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
