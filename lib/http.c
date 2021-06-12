#include <http.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define HTTP_VERSION "HTTP/1.1"

char * methods_strings[6] = {"GET", "POST", "CONNECT","DELETE","PUT","HEAD"};

#define print(...) \
	position += snprintf(buffer + position, space, ##__VA_ARGS__); \
    if ((space -= position) <= 0) return space;


/*-----------------------------------------
 *          REQUEST SYNTAX
 *-----------------------------------------
 *  METHOD <target> HTTP/1.1
 *  HEADERS: ....
 *
 *  BODY...
 */

int write_request(http_request * request, char * buffer, int space) {
    int position = 0;
    
    print("%s %s %s\r\n", methods_strings[request->method - 1], request->url, HTTP_VERSION)

    for (size_t i = 0; i < request->message.header_count; i++) {
        print("%s: %s\r\n", request->message.headers[i][0], request->message.headers[i][1])
    }

    if (request->message.body != NULL) {
        request->message.body[request->message.body_length] = 0;
        print("\r\n%s", request->message.body);
    }

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

int write_response(http_response * response, char * buffer, int space) {
    int position = 0;

    char * default_reason = NULL;
    switch (response->status) {
        case RESPONSE_OK: default_reason = "OK"; break;
        case BAD_REQUEST: default_reason = "Bad Request"; break;
        case FORBIDDEN: default_reason = "Forbidden"; break;
        case CONFLICT: default_reason = "Conflict"; break;
        case PAYLOAD_TOO_LARGE: default_reason = "Payload Too Large"; break;
        case INTERNAL_SERVER_ERROR: default_reason = "Internal Server Error"; break;
        case BAD_GATEWAY: default_reason = "Bad Gateway"; break;
        case GATEWAY_TIMEOUT: default_reason = "Gateway Timeout"; break;
    }

    if (strlen(response->reason) == 0 && default_reason != NULL)
        strncpy(response->reason, default_reason, REASON_LENGTH);
    
    print("%s %d %s\r\n", HTTP_VERSION, response->status, response->reason)

    for (size_t i = 0; i < response->message.header_count; i++) {
        print("%s: %s\r\n", response->message.headers[i][0], response->message.headers[i][1])
    }

    if (response->message.body != NULL) {
        response->message.body[response->message.body_length] = 0;
        print("\r\n%s", response->message.body);
    }
    
    return position;

}
