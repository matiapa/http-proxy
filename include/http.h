#ifndef HTTP_H
#define HTTP_H

#include <buffer.h>

#define HEADER_COLUMNS 2

typedef enum {GET, POST, CONNECT} methods;

typedef enum {CONNECTION, REQUEST, RESPONSE} item_state;

typedef enum {SUCCESS, PENDING, FAILED} parse_state;

/* REQUEST STRUCTURE */
struct request {
    methods method;
    char *** headers;
    int header_count;
    char * body;
    char * url;
    char * file;
};

/* RESPONSE STRUCTURE */
struct response {
    int status_code;
    char * status_message;
    char *** headers;
    int header_count;
    char * body;
};

/* REQUEST FACTORY */
char * create_request(struct request * request);

/* RESPONSE FACTORY */
char * create_response(struct response * response);

/* REQUEST PARSER */
parse_state parse_http_request(buffer * reqBuffer, struct request * parsedReq);

/* RESPONSE PARSER */
parse_state parse_http_response(buffer * resBuffer, struct response * parsedRes);

#endif


/* EJEMPLO ARMADO DE HEADER
    int header_count = 2;
    char *** headers = malloc(sizeof(char *)*HEADER_COLUMNS*header_count);
    char * header1[2] = {"Accept", "application/"};
    char * header2[2] = {"Accept-Encoding", "gzip"};
    headers[0] = header1;
    headers[1] = header2;
*/

/* EJEMPLO DE USO REQUEST FACTORY
    struct request request = {
            .method = GET,
            .file = "holamundo.txt",
            .headers = headers,
            .header_count = header_count
    };
    char * string = create_request(&request);
    printf("%s\n", string);
    free(string);
    free(headers);
*/

/* EJEMPLO DE USO RESPONSE FACTORY
    struct response response = {
            .status_code = 200,
            .status_message = "OK",
            .headers = headers,
            .header_count = header_count
    };
    char * string = create_response(&response);
    printf("%s\n", string);
    free(string);
    free(headers);
*/

