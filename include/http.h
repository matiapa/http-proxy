#ifndef HTTP_H
#define HTTP_H

#include <buffer.h>

/*---------------------- Size definitions ----------------------*/

// A version has exactly 8 chars 
#define VERSION_LENGTH 8

// Suggested length (RFC 7230 - Section 3.1.1), larger URLs return 414 - URI Too Long
#define URL_LENGTH 8000

// Chosen arbitrarily (RFC 7230 - Section 3.1.2), larger reasons are truncated
#define REASON_LENGTH 128           

// Chosen arbitrarily (RFC 7230 - Section 3.2.5), more headers are discarded
#define MAX_HEADERS 128

// Chosen arbitrarily (RFC 7230 - Section 3.2.5), larger headers are truncated
#define HEADER_LENGTH 512

// Chosen arbitrarily, larger bodies return 413 - Payload Too Large
#define BODY_LENGTH 1024*1024

/*---------------------- Keywords definitions ----------------------*/

typedef enum methods {GET, POST, CONNECT,DELETE,PUT,HEAD, OTHER} methods;

#define RESPONSE_OK 200
#define BAD_REQUEST 400
#define FORBIDDEN 403
#define CONFLICT 409
#define PAYLOAD_TOO_LARGE 413
#define URI_TOO_LONG 414
#define INTERNAL_SERVER_ERROR 500
#define BAD_GATEWAY 502
#define GATEWAY_TIMEOUT 504

/*---------------------- Structs definitions ----------------------*/

/* REQUEST STRUCTURE */
struct request {
    methods method;
    char url[URL_LENGTH];
    char version[VERSION_LENGTH];

    char headers[MAX_HEADERS][2][HEADER_LENGTH];
    int header_count;

    char * body;
    int body_length;
};

/* RESPONSE STRUCTURE */
struct response {
    int status_code;
    char version[VERSION_LENGTH];
    char reason[REASON_LENGTH];

    char headers[MAX_HEADERS][2][HEADER_LENGTH];
    int header_count;

    char * body;
    int body_length;
};

/* REQUEST FACTORY */
char * create_request(struct request * request);

/* RESPONSE FACTORY */
char * create_response(struct response * response);

int copy(char * dst, char * src);

#endif
