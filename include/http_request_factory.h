#ifndef PC_2021A_06_HTTP_REQUEST_FACTORY_H
#define PC_2021A_06_HTTP_REQUEST_FACTORY_H

#define HEADER_COLUMNS 2

typedef enum {GET, POST, CONNECT} methods;

/* REQUEST STRUCTURE */
struct request {
    methods method; // usar enum methods
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
char * request_factory(struct request * request);

/* RESPONSE FACTORY */
char * response_factory(struct response * response);

#endif //PC_2021A_06_HTTP_REQUEST_FACTORY_H


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
    char * string = request_factory(&request);
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
    char * string = response_factory(&response);
    printf("%s\n", string);
    free(string);
    free(headers);
*/

