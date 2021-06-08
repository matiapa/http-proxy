#include <logger.h>
#include <errno.h>
#include <string.h>
#include <buffer.h>
#include <stm.h>
#include <selector.h>
#include <tcp_utils.h>
#include <http_parser.h>
#include <address.h>
#include <ctype.h>
#include <proxy_stm.h>

/* -------------------------------------- PROXY STATES -------------------------------------- */

enum proxy_state {
    
    /*
     * Sends CONNECT response message to client
     *
     * Interests:
     *   - Client: OP_WRITE
     *
     * Transitions:
     *   - CONNECT_WRITE    While message is not ready
     *   - REQUEST_READ     When message is over
     *   - ERROR_STATE            Parsing/IO error
    */
    CONNECT_WRITE,

    /*
     * Recieves an HTTP request from client
     *
     * Interests:
     *   - Client: OP_READ
     *   - Target: OP_NOOP
     *
     * Transitions:
     *   - REQUEST_READ     While request message is not over
     *   - REQUEST_WRITE  When request message is over
     *   - ERROR_STATE            Parsing/IO error
    */
    REQUEST_READ,

    /*
     * Forwards client HTTP request to target
     *
     * Interests:
     *   - Client: OP_NOOP
     *   - Target: OP_WRITE
     *
     * Transitions:
     *   - REQUEST_WRITE  While request message is not over
     *   - RESPONSE_READ    When request message is over
     *   - ERROR_STATE            IO error
    */
    REQUEST_WRITE,

    /*
     * Recieves an HTTP request from target
     *
     * Interests:
     *   - Client: OP_NOOP
     *   - Target: OP_READ
     *
     * Transitions:
     *   - RESPONSE_READ    While response message is not over
     *   - RESPONSE_WRITE When response message is over
     *   - ERROR_STATE            Parsing/IO error
    */
    RESPONSE_READ,

    /*
     * Forwards target HTTP response to client
     *
     * Interests:
     *   - Client: OP_WRITE
     *   - Target: OP_NOOP
     *
     * Transitions:
     *   - RESPONSE_WRITE While response message is not over
     *   - REQUEST_READ     When response message is over
     *   - ERROR_STATE            IO error
    */
    RESPONSE_WRITE,

    /*
     * Sends Clients last messages and gracefully shuts down
     *
     * Interests:
     *   - Client: OP_READ
     *   - Target: OP_WRITE
     *
     * Transitions:
     *  
     *   - END      When response message is over
     *   - ERROR_STATE            IO error
    */
    CLIENT_CLOSE_CONNECTION,

    /*
     * Sends Target last messages and gracefully shuts down
     *
     * Interests:
     *   - Client: OP_WRITE
     *   - Target: OP_READ 
     *
     * Transitions:
     *  
     *   - END      When response message is over
     *   - ERROR_STATE            IO error
    */
    TARGET_CLOSE_CONNECTION,

    END,

    ERROR_STATE,

};


/* -------------------------------------- HANDLERS PROTOTYPES -------------------------------------- */

/* ------------------------------------------------------------
  Sends connection response to client.
------------------------------------------------------------ */
static unsigned connect_write_ready(struct selector_key *key);

/* ------------------------------------------------------------
  Reads HTTP requests from client.
------------------------------------------------------------ */
static unsigned request_read_ready(struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP requests to target.
------------------------------------------------------------ */
static unsigned request_write_ready(struct selector_key *key);

/* ------------------------------------------------------------
  Reads HTTP responses from target.
------------------------------------------------------------ */
static unsigned response_read_ready(struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP responses to client.
------------------------------------------------------------ */
static unsigned response_write_ready(struct selector_key *key);

/* ------------------------------------------------------------
  Sends last messages from client to target then closes connection
------------------------------------------------------------ */
static unsigned client_close_connection_arrival(const unsigned state, struct selector_key *key);

/* ------------------------------------------------------------
  Sends last messages from target to client then closes connection
------------------------------------------------------------ */
static unsigned target_close_connection_arrival(const unsigned state, struct selector_key *key);
/* ------------------------------------------------------------
  Trap state handler
------------------------------------------------------------ */
static unsigned end_arrival(const unsigned state, struct selector_key *key);

/* ------------------------------------------------------------
  Handles proxy errors.
------------------------------------------------------------ */
static unsigned error_arrival(const unsigned state, struct selector_key *key);

/* ------------------------------------------------------------
  Notifies proxy errors.
------------------------------------------------------------ */
static unsigned error_write_ready(struct selector_key *key);


/* -------------------------------------- PROCESSORS PROTOTYPES -------------------------------------- */

/* ------------------------------------------------------------
  Processes an HTTP request and returns next state.
------------------------------------------------------------ */
static unsigned process_request(struct selector_key * key);

/* ------------------------------------------------------------
  Initiates a connection to target and returns next state.
------------------------------------------------------------ */
static unsigned connect_target(struct selector_key * key, char * target, int port);

/* ------------------------------------------------------------
  Processes request headers according to RFC 7230 specifications
------------------------------------------------------------ */
static void process_request_headers(struct request * req, char * target_host, char * proxy_host);


/* -------------------------------------- STATE MACHINE DEFINITION -------------------------------------- */


static const struct state_definition state_defs[] = {
    {
        .state            = CONNECT_WRITE,
        .client_interest  = OP_WRITE,
        .target_interest  = OP_NOOP,
        .on_write_ready   = connect_write_ready,
    },
    {
        .state            = REQUEST_READ,
        .client_interest  = OP_READ,
        .target_interest  = OP_NOOP,
        .rst_buffer       = READ_BUFFER | WRITE_BUFFER,
        .on_read_ready    = request_read_ready,
    },
    {
        .state            = REQUEST_WRITE,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_WRITE,
        .on_write_ready   = request_write_ready,
    },
    {
        .state            = RESPONSE_READ,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_READ,
        .rst_buffer       = READ_BUFFER | WRITE_BUFFER,
        .on_read_ready    = response_read_ready,
    },
    {
        .state            = RESPONSE_WRITE,
        .client_interest  = OP_WRITE,
        .target_interest  = OP_NOOP,
        .on_write_ready   = response_write_ready,
    },
    {
        .state            = CLIENT_CLOSE_CONNECTION,
        .client_interest  = OP_READ,
        .target_interest  = OP_WRITE,
        .on_arrival       = client_close_connection_arrival
    },
    {
        .state            = TARGET_CLOSE_CONNECTION,
        .client_interest  = OP_WRITE,
        .target_interest  = OP_READ,
        .on_arrival       = target_close_connection_arrival
    },
    {
        .state            = END,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_NOOP,
        .on_arrival       = end_arrival
    },
    {
        .state            = ERROR_STATE,
        .client_interest  = OP_WRITE,
        .target_interest  = OP_NOOP,
        .rst_buffer       = WRITE_BUFFER,
        .on_arrival       = error_arrival,
        .on_write_ready   = error_write_ready
    }
};


state_machine proto_stm = {
    .initial = REQUEST_READ,
    .states = state_defs,
    .max_state = ERROR_STATE
};

typedef struct proxy_error {
    int status_code;
    unsigned next_state;
} proxy_error;

proxy_error error;

#define log_error(_description) \
    log(ERROR, "At state %d: %s", key->item->stm.current->state, _description);

#define notify_error(_status_code, _next_state) \
    error.status_code = _status_code; \
    error.next_state = _next_state; \
    return ERROR_STATE; \

/* -------------------------------------- HANDLERS IMPLEMENTATIONS -------------------------------------- */

static unsigned connect_write_ready(struct selector_key *key) {

    if (! buffer_can_read(&(key->item->write_buffer)))
        return CONNECT_WRITE;

    // Read response bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->src_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF)
            log_error("Failed to write connect response to client");
        return END;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->src_socket);

    if ((size_t) sentBytes < size)
        return CONNECT_WRITE;

    return REQUEST_READ;

}


static unsigned request_read_ready(struct selector_key *key) {

    key->item->last_activity = time(NULL);

    if (! buffer_can_write(&(key->item->read_buffer))) {
        log_error("Read buffer limit reached")
        notify_error(PAYLOAD_TOO_LARGE, REQUEST_READ);
    }

    // Read request bytes into read buffer

    size_t space;
    uint8_t * raw_req = buffer_write_ptr(&(key->item->read_buffer), &space);
    ssize_t readBytes = read(key->src_socket, raw_req, space);

    if(readBytes < 0) {
        if(errno != EBADF)
            log_error("Failed to read from client");
        return CLIENT_CLOSE_CONNECTION;
    }

    if (readBytes <= 0)
        return CLIENT_CLOSE_CONNECTION;

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %ld bytes from socket %d", readBytes, key->src_socket);

    // Process the request

    return process_request(key);

}


static unsigned request_write_ready(struct selector_key *key) {

    if (! buffer_can_read(&(key->item->write_buffer)))
        return REQUEST_WRITE;

    // Read request bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->dst_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF)
            log_error("Failed to write request to target");
        return TARGET_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->dst_socket);

    if ((size_t) sentBytes < size)
        return REQUEST_WRITE;

    return RESPONSE_READ;

}


static unsigned response_read_ready(struct selector_key *key) {

    if (! buffer_can_write(&(key->item->read_buffer))) {
        log_error("Read buffer limit reached")
        notify_error(BAD_GATEWAY, REQUEST_READ);
    }

    // Read response bytes into read buffer

    size_t space;
    uint8_t * raw_res = buffer_write_ptr(&(key->item->read_buffer), &space);
    ssize_t readBytes = read(key->dst_socket, raw_res, space);

    if(readBytes < 0) {
        if(errno != EBADF)
            log_error("Failed to read response from target");
        return TARGET_CLOSE_CONNECTION;
    }

    if (readBytes <= 0)
        return TARGET_CLOSE_CONNECTION;

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %ld bytes from socket %d", readBytes, key->dst_socket);

    // Parse the response

    // TODO: Add parser and handle pending message case

    // Write processed response bytes into write buffer

    uint8_t * raw_res_proc = buffer_write_ptr(&(key->item->write_buffer), &space);
    strncpy((char *) raw_res_proc, (char *) raw_res, readBytes);
    buffer_write_adv(&(key->item->write_buffer), readBytes);

    return RESPONSE_WRITE;

}


static unsigned response_write_ready(struct selector_key *key) {

    if (! buffer_can_read(&(key->item->write_buffer)))
        return RESPONSE_WRITE;

    // Read response bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->src_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF)
            log_error("Failed to write response to client");
        return CLIENT_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->src_socket);

    if ((size_t) sentBytes < size)
        return RESPONSE_WRITE;

    return REQUEST_READ;

}


#pragma GCC diagnostic ignored "-Wunused-parameter"
static unsigned error_arrival(const unsigned state, struct selector_key *key) {

    struct response res = { .status_code = error.status_code };
    char * raw_res = create_response(&res);

    size_t space;
    char * ptr = (char *) buffer_write_ptr(&(key->item->write_buffer), &space);

    strncpy(ptr, raw_res, space);
    buffer_write_adv(&(key->item->write_buffer), strlen(ptr));

    log(ERROR, "Writed %ld bytes to write buffer", strlen(ptr));

    log(ERROR, "%s", raw_res);

    return ERROR_STATE;

}


static unsigned error_write_ready(struct selector_key *key) {

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->src_socket, ptr, size);

    log(ERROR, "Read %ld bytes from write buffer", sentBytes);

    if (sentBytes < 0) {
        log(ERROR, "Failed to notify error to client");
        return END;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    if ((size_t) sentBytes < size)
        return ERROR_STATE;

    return error.next_state;

}


static unsigned client_close_connection_arrival(const unsigned state, struct selector_key *key) {

    log(DEBUG, "Client closed connection from socket %d", key->src_socket);

    // Read last bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->dst_socket, ptr, size);

    if (sentBytes < 0)
        return END;

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->dst_socket);

    if ((size_t) sentBytes < size)
        return CLIENT_CLOSE_CONNECTION;

    return END;

}


static unsigned target_close_connection_arrival(const unsigned state, struct selector_key *key) {

    log(DEBUG, "Target Closed connection from socket %d", key->dst_socket);

    // Read last bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->src_socket, ptr, size);

    if (sentBytes < 0)
        return END;

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->src_socket);

    if ((size_t) sentBytes < size)
        return TARGET_CLOSE_CONNECTION;

    return END;

}


static unsigned end_arrival(const unsigned state, struct selector_key *key){

    item_kill(key->s, key->item);

    return END;
    
}


/* -------------------------------------- PROCESSORS IMPLEMENTATIONS -------------------------------------- */


static unsigned process_request(struct selector_key * key) {

    // Instantiate a request struct or use previous one if it exists

    if(key->item->data == NULL)
        key->item->data = calloc(1, sizeof(struct request));
    
    struct request * request = key->item->data;

    // Parse the request and check for pending and failure cases

    parse_state parser_state = http_parser_parse(&(key->item->read_buffer), request, &(key->item->parser_data));

    if (parser_state == PENDING)
        return REQUEST_READ;

    if (parser_state == FAILED) {
        log_error("Invalid request received");
        notify_error(BAD_REQUEST, REQUEST_READ);
    }

    if (request->method == CONNECT) {

        // The request method is CONNECT, URL must be in authority form
        // and a new connection must be established

        struct url url;
        parse_url(request->url, &url);

        unsigned ret = connect_target(key, url.hostname, url.port);
        if (ret == ERROR_STATE)
            return ret;

        // Write response bytes into write buffer

        struct response res = { .status_code = RESPONSE_OK };
        char * raw_res = create_response(&res);

        size_t space;
        uint8_t * ptr = buffer_write_ptr(&(key->item->write_buffer), &space);

        strcpy((char *) ptr, raw_res);
        buffer_write_adv(&(key->item->write_buffer), strlen(raw_res));

        return CONNECT_WRITE;

    } else {

        // The request method is a traditional one, URL must be in
        // in absolute-form or origin-form

        struct url url = {0};
        parse_url(request->url, &url);
        
        if (strlen(url.hostname) > 0) {
            // URL is in absolute-form, initiate a new connection
            // and proceed to forward request

            unsigned ret = connect_target(key, url.hostname, url.port);
            if (ret == ERROR_STATE)
                return ret;
        }

        // Check that a target connection is established

        if (key->item->target_socket < 0) {
            log_error("There is no target connection");
            notify_error(CONFLICT, REQUEST_READ);
        }

        // Process request headers

        char proxy_hostname[128];
        if(strlen(proxy_conf.viaProxyName) > 0) {
            strncpy(proxy_hostname, proxy_conf.viaProxyName, 128);
        } else {
            get_machine_fqdn(proxy_hostname);
        }
        
        process_request_headers(request, url.hostname, proxy_hostname);

        // Write processed request bytes into write buffer

        char * raw_req = create_request(request);
        size_t size = strlen(raw_req);

        size_t space;
        char * ptr = (char *) buffer_write_ptr(&(key->item->write_buffer), &space);

        int writeBytes = size < space ? size : space;
        strncpy((char *) ptr, (char *) raw_req, writeBytes);
        buffer_write_adv(&(key->item->write_buffer), writeBytes);

        return REQUEST_WRITE;

    }
    
}


static unsigned connect_target(struct selector_key * key, char * target_host, int target_port) {

    // If there is an established connection to same target, return

    if (key->item->target_socket > 0 && strcmp(key->item->target_name, target_host) == 0)
        return 0;

    // If there is an established connection to another target, close it

    if (key->item->target_socket > 0 && strcmp(key->item->target_name, target_host) != 0)
        close(key->item->target_socket);

    // Get hostname and port from target

    log(DEBUG, "Connection requested to %s:%d", target_host, target_port);

    // Check that target is not blacklisted

    if (strstr(proxy_conf.targetBlacklist, target_host) != NULL) {
        log(INFO, "Rejected connection to %s due to target blacklist", target_host);
        notify_error(FORBIDDEN, REQUEST_READ);
    }

    // Check that target is not the proxy itself

    if (strcmp(target_host, "localhost") == 0 && target_port == proxy_conf.proxyArgs.proxy_port) {
        log(INFO, "Rejected connection to proxy itself");
        notify_error(FORBIDDEN, REQUEST_READ);
    }

    // Open connection with target

    int targetSocket = create_tcp_client(target_host, target_port);
    if (targetSocket < 0) {
        log_error("Failed to connect to target");
        notify_error(INTERNAL_SERVER_ERROR, REQUEST_READ);
    }

    key->item->target_socket = targetSocket;

    return 0;

}


#define remove_array_elem(array, pos, size) \
    memcpy(array+pos, array+pos+1, size-pos-1)

#define rtrim(s) \
    char* back = s + strlen(s); \
    while(isspace(*--back)); \
    *(back+1) = 0;

static void process_request_headers(struct request * req, char * target_host, char * proxy_host) {

    char * raw_req = create_request(req);
    log(DEBUG, "Original request");
    log(DEBUG, raw_req);
    free(raw_req);

    bool replaced_host_header = false;
    bool replaced_via_header = false;
    bool close_detected = false;

    for (int i=0; i < req->header_count; i++) {

        // Right trim header names

        rtrim(req->headers[i][0]);

        // Replace Host header if target hostname is not empty

        if (strcmp(req->headers[i][0], "Host") == 0 && strlen(target_host) > 0) {
            strcpy(req->headers[i][1], target_host);
            replaced_host_header = true;
        }

        // Replace Via header appending proxy hostname

        if (strcmp(req->headers[i][0], "Via") == 0) {
            char new_token[128];
            sprintf(new_token, ", 1.1 %s", proxy_host);

            strcat(req->headers[i][1], new_token);
            
            replaced_via_header = true;
        }

        // Remove headers listed on Connection header

        if (strcmp(req->headers[i][0], "Connection") == 0) {
            char * connection_headers = req->headers[i][1];

            if (strstr(connection_headers, "Close"))
                close_detected = true;

            for(int j=0; j < req->header_count; j++) {
                if (strstr(connection_headers, req->headers[j][0])) {
                    remove_array_elem(req->headers, j, req->header_count);
                    req->header_count -= 1;
                }
            }
        }


    }

    // If a Host header was not present but a hostname was given, add it

    if(!replaced_host_header && strlen(target_host) > 0) {
        req->header_count += 1;
        strcpy(req->headers[req->header_count - 1][0], "Host");
        strcpy(req->headers[req->header_count - 1][1], target_host);
    }

    // If a Via header was not present, add it

    if(!replaced_via_header) {
        req->header_count += 1;
        sprintf(req->headers[req->header_count - 1][0], "Via");
        sprintf(req->headers[req->header_count - 1][1], "1.1 %s", proxy_host);
    }

    // TODO: Handle close detected

    raw_req = create_request(req);
    log(DEBUG, "Processed request");
    log(DEBUG, raw_req);
    free(raw_req);

}
