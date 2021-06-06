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


void parse_http_request(uint8_t * readBuffer,struct request *httpRequest, size_t readBytes);

#endif //PC_2021A_06_HTTP_PARSER_H
