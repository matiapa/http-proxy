#include <http.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define STRING_SIZE 300
#define HTTP_VERSION "HTTP/1.1"


int requestFirstLine(char * string, struct request * request);

int responseFirstLine(char * string, struct response * response);

int headersSection(char * string, char headers[MAX_HEADERS][2][HEADER_LENGTH], int header_count);


char * methods_strings[3] = {"GET", "POST", "CONNECT"};

int status_code_num[8] = {200, 400, 403, 409, 413, 500, 502, 504};

char * status_code_message[8] = {
    "OK", "Bad Request", "Forbidden", "Conflict", "Payload Too Large",
    "Internal Server Error", "Bad Gateway", "Gateway Timeout"
};


/*-----------------------------------------
 *          REQUEST SYNTAX
 *-----------------------------------------
 *  METHOD /<target> HTTP/1.1
 *  HEADERS: ....
 *
 *  BODY...
 */

 char * create_request(struct request * request) {
     char * string = (char *) malloc(STRING_SIZE);
     memset(string, 0, STRING_SIZE);
     int position = 0;

     if (request->method == POST || request->method == GET || request->method == CONNECT) {
         position = requestFirstLine(string, request);
         if (position < 0) return NULL;
     } else {
         free(string);
         return NULL;
     }

    if (request->header_count > 0) position += headersSection(string + position, request->headers, request->header_count);

    position += copy(string + position, "\r\n");

    if (request->body_length > 0) {
        memcpy(string + position, request->body, request->body_length);
        position += request->body_length;
    }

    string = realloc(string, position);
    string[position] = '\0';
    return string;
}

int requestFirstLine(char * string, struct request * request) {
    int position = snprintf(string, STRING_SIZE, "%s ", methods_strings[request->method]);

    if ((request->method == CONNECT || request->method == GET || request->method == POST)) {
        position += copy(string + position, request->url);
    } else {
        return -1;
    }

    position += snprintf(string + position, STRING_SIZE - position, " %s\r\n", HTTP_VERSION);
    return position;
}



/*-----------------------------------------
 *          RESPONSE SYNTAX
 *-----------------------------------------
 *  HTTP/1.1 STATUS_CODE MESSAGE
 *  HEADERS: ....
 *
 *  BODY...
 */

char * create_response(struct response * response) {

    char * string = (char *) malloc(STRING_SIZE);
    memset(string, 0, STRING_SIZE);

    int position = responseFirstLine(string, response);

    if (response->header_count > 0)
        position += headersSection(string + position, response->headers, response->header_count);
    else position += copy(string + position, "\r\n");

    if (response->body_length > 0) position += copy(string + position, response->body);

    string = realloc(string, position);
    string[position] = '\0';

    return string;
    
}


int responseFirstLine(char * string, struct response * response) {

    return snprintf(
        string, STRING_SIZE, "%s %d %s\n", HTTP_VERSION,
        status_code_num[response->status_code], status_code_message[response->status_code]
    );
}


/*-----------------------------------------
 *  Funciones axuiliares
 *-----------------------------------------*/

int headersSection(char * string, char headers[MAX_HEADERS][2][HEADER_LENGTH], int header_count) {
    int position = 0;
    for (int i = 0; i < header_count; i++)
        position += snprintf(string + position, STRING_SIZE - position, "%s: %s\r\n", headers[i][0], headers[i][1]);

    return position;
}

int copy(char * dst, char * src) {
    int length = strlen(src);
    memcpy(dst, src, length);
    return length;
}


