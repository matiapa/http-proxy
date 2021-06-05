#include <http.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BLOCK_SIZE 200
#define STRING_SIZE 2000
#define HTTP_VERSION "HTTP/1.1"

int copy(char * dst, char * src);
int requestFirstLine(char * string, struct request * request);
int responseFirstLine(char * string, struct response * request);
int headersSection(char * string, char ** headers[2], int header_count);

char * methods_strings[3] = {"GET", "POST", "CONNECT"};



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

    if (request->headers != NULL) position += headersSection(string + position, request->headers, request->header_count);

    position += copy(string + position, "\n");

    if (request->body != NULL) position += copy(string + position, request->body);

    string = realloc(string, position);
    string[position] = '\0';
    return string;
}

int requestFirstLine(char * string, struct request * request) {
    int position = snprintf(string, STRING_SIZE, "%s ", methods_strings[request->method]);

    if ((request->method == CONNECT || request->method == GET) && request->url != NULL) {
        position += copy(string + position, request->url);
    } else if (request->method == GET || request->method == POST) {
        position += copy(string + position, "/");
        if (request->file != NULL) position += copy(string + position, request->file);
    } else {
        return -1;
    }

    position += snprintf(string + position, STRING_SIZE - position, " %s\n", HTTP_VERSION);
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
    if (position < 0) {
        free(string);
        return NULL;
    }

    if (response->headers != NULL)
        position += headersSection(string + position, response->headers, response->header_count);
    else position += copy(string + position, "\n");

    if (response->body != NULL) position += copy(string + position, response->body);

    string = realloc(string, position);
    string[position] = '\0';
    return string;
}


int responseFirstLine(char * string, struct response * response) {

    if (response->status_message == NULL || response->status_code < 100)
        return -1;

    return snprintf(string, STRING_SIZE, "%s %d %s\n", HTTP_VERSION, response->status_code, response->status_message);
}


/*-----------------------------------------
 *  Funciones axuiliares
 *-----------------------------------------*/

int headersSection(char * string, char *** headers, int header_count) {
    int position = 0;
    for (int i = 0; i < header_count; i++)
        position += snprintf(string + position, STRING_SIZE - position, "%s: %s\n", headers[i][0], headers[i][1]);

    return position;
}

int copy(char * dst, char * src) {
    int length = strlen(src);
    memcpy(dst, src, length);
    return length;
}


