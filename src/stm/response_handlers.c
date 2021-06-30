#include <logger.h>
#include <errno.h>
#include <string.h>
#include <buffer.h>
#include <stm.h>
#include <tcp_utils.h>
#include <http_request_parser.h>
#include <http_response_parser.h>
#include <http.h>
#include <address.h>
#include <ctype.h>
#include <proxy_stm.h>
#include <statistics.h>
#include <dissector.h>
#include <arpa/inet.h>
#include <proxy_stm.h>

#pragma GCC diagnostic ignored "-Wunused-parameter"


/* -------------------------------------- AUXILIARS PROTOTYPES -------------------------------------- */


/* ------------------------------------------------------------
  Processes an HTTP response and returns next state.
------------------------------------------------------------ */
static unsigned process_response(selector_key_t * key);

/* ------------------------------------------------------------
  Processes response headers according to RFC 7230 specs.
------------------------------------------------------------ */
static void process_response_headers(http_response * res, char * proxy_host);


/* -------------------------------------- HANDLERS IMPLEMENTATIONS -------------------------------------- */


unsigned response_read_ready(unsigned int state, selector_key_t *key) {

    if (! buffer_can_write(&(I(key)->read_buffer))) {
        log_error("Read buffer limit reached");
        return notify_error(key, BAD_GATEWAY, REQUEST_READ);
    }

    // Read response bytes into read buffer

    size_t space;
    uint8_t * raw_res = buffer_write_ptr(&(I(key)->read_buffer), &space);
    ssize_t readBytes = read(I(key)->target_socket, raw_res, space);

    if(readBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to read response from target");
        return TARGET_CLOSE_CONNECTION;
    }

    if (readBytes == 0)
        return TARGET_CLOSE_CONNECTION;

    buffer_write_adv(&(I(key)->read_buffer), readBytes);

    log(DEBUG, "Received %lu bytes from socket %d", (size_t) readBytes, I(key)->target_socket);

    // Calculate statistics

    add_bytes_recieved(readBytes);

    // Process the response

    return process_response(key);

}


unsigned response_forward_ready(unsigned int state, selector_key_t *key) {

    // Read response bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(I(key)->write_buffer), &size);
    ssize_t sentBytes = write(I(key)->client_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write response to client");
        return CLIENT_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(I(key)->write_buffer), sentBytes);

    log(DEBUG, "Sent %lu bytes to socket %d", (size_t) sentBytes, I(key)->client_socket);

    // Calculate statistics

    add_sent_bytes(sentBytes);

    if ((size_t) sentBytes < size)
        return RESPONSE_FORWARD;

       
    if (I(key)->res_parser.response.message.body_length == 0) {
        // No body

        http_request_parser_reset(&(I(key)->req_parser));
        http_response_parser_reset(&(I(key)->res_parser));
        
        proxy_item_reset(key);
        
        return REQUEST_READ;
    } else if (buffer_can_read(&(I(key)->read_buffer))) {
        // Body present, read some bytes

        size_t remaining;
        buffer_read_ptr(&(I(key)->read_buffer), &remaining);
        I(key)->res_parser.message_parser.current_body_length += remaining;

        return RES_BODY_FORWARD;
    } else {
        // Body present, didn't read bytes

        return RES_BODY_READ;
    }
    

}


unsigned res_body_read_ready(unsigned int state, selector_key_t *key) {

    I(key)->last_activity = time(NULL);

    if (! buffer_can_write(&(I(key)->read_buffer)))
        return RES_BODY_FORWARD;

    // Read body bytes into read buffer

    size_t space;
    uint8_t * body = buffer_write_ptr(&(I(key)->read_buffer), &space);
    ssize_t readBytes = read(I(key)->target_socket, body, space);

    if(readBytes <= 0) {
        if(readBytes < 0 && errno != EBADF && errno != EPIPE)
            log_error("Failed to read from target");
        return TARGET_CLOSE_CONNECTION;
    }

    buffer_write_adv(&(I(key)->read_buffer), readBytes);

    log(DEBUG, "Received %lu body bytes from socket %d", (size_t) readBytes, I(key)->target_socket);

    // Calculate statistics

    add_bytes_recieved(readBytes);

    http_response_parser * rp = &(I(key)->res_parser);

    rp->message_parser.current_body_length += readBytes;

    if ((rp->message_parser.current_body_length) < rp->response.message.body_length)
        return RES_BODY_READ;
    else
        return RES_BODY_FORWARD;

}


unsigned res_body_forward_ready(unsigned int state, selector_key_t *key) {

    // Will read the content directly from read buffer

    // Read body bytes from read buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(I(key)->read_buffer), &size);
    ssize_t sentBytes = write(I(key)->client_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write body to client");
        return CLIENT_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(I(key)->read_buffer), sentBytes);

    log(DEBUG, "Sent %lu body bytes to socket %d", (size_t) sentBytes, I(key)->client_socket);

    // Calculate statistics

    add_sent_bytes(sentBytes);

    http_response_parser * rp = &(I(key)->res_parser);

    if ((size_t) sentBytes < size) {
        return RES_BODY_FORWARD;
    } else if ((rp->message_parser.current_body_length) < rp->response.message.body_length) {
        return RES_BODY_READ;
    } else {
        http_request_parser_reset(&(I(key)->req_parser));
        http_response_parser_reset(&(I(key)->res_parser));
        return REQUEST_READ;
    }

}


/* -------------------------------------- AUXILIARS IMPLEMENTATIONS -------------------------------------- */


static unsigned process_response(selector_key_t * key) {

    // Parse the response and check for pending and failure cases

    bool ignore_length = I(key)->req_parser.request.method == HEAD ? true : false;

    parse_state parser_state = http_response_parser_parse(
        &(I(key)->res_parser), &(I(key)->read_buffer), ignore_length
    );

    if (parser_state == PENDING)
        return RESPONSE_READ;

    if (parser_state == FAILED) {
        return notify_error(key, BAD_GATEWAY, REQUEST_READ);
    }

    print_Access(inet_ntoa(I(key)->client.sin_addr), ntohs(I(key)->client.sin_port), I(key)->req_parser.request.url, 
    I(key)->req_parser.request.method, I(key)->res_parser.response.status);

    http_response * response = &(I(key)->res_parser.response);

    // Process response headers

    char proxy_hostname[VIA_PROXY_NAME_SIZE] = {0};
    if(strlen(proxy_conf.viaProxyName) > 0) {
        strncpy(proxy_hostname, proxy_conf.viaProxyName, VIA_PROXY_NAME_SIZE);
    } else {
        get_machine_fqdn(proxy_hostname);
    }

    process_response_headers(response, proxy_hostname);

    // Write processed response bytes into write buffer

    size_t space;
    char * ptr = (char *) buffer_write_ptr(&(I(key)->write_buffer), &space);

    int written = write_response(response, ptr, space, false);

    buffer_write_adv(&(I(key)->write_buffer), written);

    // Go to forward response state

    return RESPONSE_FORWARD;
        
}


static void process_response_headers(http_response * res, char * proxy_host) {

    bool replaced_via_header = false;
    bool close_detected = false;

    for (size_t i=0; i < res->message.header_count; i++) {

        // Right trim header names

        rtrim(res->message.headers[i][0]);

        // Replace Via header appending proxy hostname

        if (strcmp(res->message.headers[i][0], "Via") == 0) {
            char new_token[128];
            sprintf(new_token, ", 1.1 %s", proxy_host);

            strcat(res->message.headers[i][1], new_token);
            
            replaced_via_header = true;
        }

        // Remove headers listed on Connection header

        if (strcmp(res->message.headers[i][0], "Connection") == 0) {
            char * connection_headers = res->message.headers[i][1];

            if (strstr(connection_headers, "Close"))
                close_detected = true;

            for(size_t j=0; j < res->message.header_count; j++) {
                if (strstr(connection_headers, res->message.headers[j][0])) {
                    remove_array_elem(res->message.headers, j, res->message.header_count);
                    res->message.header_count -= 1;
                }
            }
        }

    }

    // If a Via header was not present, add it

    if(!replaced_via_header) {
        res->message.header_count += 1;
        sprintf(res->message.headers[res->message.header_count - 1][0], "Via");
        sprintf(res->message.headers[res->message.header_count - 1][1], "1.1 %s", proxy_host);
    }

    /// TODO: Handle close detected

    if (close_detected) {
        log(DEBUG, "Should close connection");
    }

}
