#ifndef HTTP_H
#define HTTP_H

#include <buffer.h>

#define URL_LENGTH 50
#define VERSION_LENGTH 50
#define MAX_HEADERS 30
#define HEADER_LENGTH 30
#define BODY_LENGTH 1024
#define REASON_LENGTH 128

typedef enum {GET, POST, CONNECT, OTHER} methods;

typedef enum {CONNECTION, REQUEST, RESPONSE} item_state;

typedef enum {
    RESPONSE_OK, BAD_REQUEST, FORBIDDEN, CONFLICT, PAYLOAD_TOO_LARGE,
    INTERNAL_SERVER_ERROR, BAD_GATEWAY, GATEWAY_TIMEOUT
} status_code;

/* REQUEST STRUCTURE */
struct request {
    methods method;
    char url[URL_LENGTH];
    char version[VERSION_LENGTH];

    char headers[MAX_HEADERS][2][HEADER_LENGTH];
    int header_count;

    char body[BODY_LENGTH];
    int body_length;
};

/* RESPONSE STRUCTURE */
struct response {
    int status_code;
    char headers[MAX_HEADERS][2][HEADER_LENGTH];
    int header_count;
    char body[BODY_LENGTH];
    int body_length;
};

/* REQUEST FACTORY */
char * create_request(struct request * request);

/* RESPONSE FACTORY */
char * create_response(struct response * response);

int copy(char * dst, char * src);

#endif

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

