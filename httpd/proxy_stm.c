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

// Many of the state transition handlers don't use the state param so we are ignoring this warning
#pragma GCC diagnostic ignored "-Wunused-parameter"

/* -------------------------------------- PROXY STATES -------------------------------------- */

enum proxy_state {
    /*
     * Recieves an HTTP request from client
     *
     * Interests:
     *   - Client: OP_READ
     *   - Target: OP_NOOP
     *
     * Transitions:
     *   - REQUEST_READ         While request message is not over
     *   - REQUEST_FORWARD      When request message is over
     *   - ERROR_STATE          Parsing/IO error
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
     *   - REQUEST_FORWARD      While request message is not over
     *   - RESPONSE_READ        When request message is over
     *   - ERROR_STATE          IO error
    */
    REQUEST_FORWARD,

    /*
     * Recieves an HTTP request from target
     *
     * Interests:
     *   - Client: OP_NOOP
     *   - Target: OP_READ
     *
     * Transitions:
     *   - RESPONSE_READ        While response message is not over
     *   - RESPONSE_FORWARD     When response message is over
     *   - ERROR_STATE          Parsing/IO error
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
     *   - RESPONSE_FORWARD     While response message is not over
     *   - REQUEST_READ         When response message is over
     *   - ERROR_STATE          IO error
    */
    RESPONSE_FORWARD,

    /*
     * Sends CONNECT response message to client
     *
     * Interests:
     *   - Client: OP_WRITE
     *
     * Transitions:
     *   - CONNECT_RESPONSE     While response message is not over
     *   - TCP_TUNNEL           When response message is over
     *   - ERROR_STATE          IO error
    */
    CONNECT_RESPONSE,

    /*
     * Enables free TCP communication among peers
     *
     * Interests:
     *   - Client: OP_READ, OP_WRITE
     *   - Target: OP_READ, OP_WRITE
     *
     * Transitions:
     *   - ERROR_STATE      IO error
    */
    TCP_TUNNEL,

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
  Reads HTTP requests from client.
------------------------------------------------------------ */
static unsigned request_read_ready(struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP requests to target.
------------------------------------------------------------ */
static unsigned request_forward_ready(struct selector_key *key);

/* ------------------------------------------------------------
  Reads HTTP responses from target.
------------------------------------------------------------ */
static unsigned response_read_ready(struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP responses to client.
------------------------------------------------------------ */
static unsigned response_forward_ready(struct selector_key *key);

/* ------------------------------------------------------------
  Sends connection response to client.
------------------------------------------------------------ */
static unsigned connect_response_ready(struct selector_key *key);

/* ------------------------------------------------------------
  Reads TCP traffic from client or target. 
------------------------------------------------------------ */
static unsigned tcp_tunnel_read_ready(struct selector_key *key);

/* ------------------------------------------------------------
  Forwards TCP traffic to corresponding peer.
------------------------------------------------------------ */
static unsigned tcp_tunnel_forward_ready(struct selector_key *key);

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
  Notifies proxy errors.
------------------------------------------------------------ */
static unsigned error_write_ready(struct selector_key *key);


/* -------------------------------------- AUXILIARS PROTOTYPES -------------------------------------- */

/* ------------------------------------------------------------
  Writes error information on buffer and returns error state.
------------------------------------------------------------ */
static unsigned notify_error(struct selector_key *key, int status_code, unsigned next_state);

/* ------------------------------------------------------------
  Processes an HTTP request and returns next state.
------------------------------------------------------------ */
static unsigned process_request(struct selector_key * key);

/* ------------------------------------------------------------
  Initiates a connection to target and returns next state.
------------------------------------------------------------ */
static unsigned connect_target(struct selector_key * key, char * target, int port);

/* ------------------------------------------------------------
  Processes request headers according to RFC 7230 specs.
------------------------------------------------------------ */
static void process_request_headers(struct request * req, char * target_host, char * proxy_host);


/* -------------------------------------- STATE MACHINE DEFINITION -------------------------------------- */


static const struct state_definition state_defs[] = {
    {
        .state            = REQUEST_READ,
        .client_interest  = OP_READ,
        .target_interest  = OP_NOOP,
        .rst_buffer       = READ_BUFFER | WRITE_BUFFER,
        .description      = "REQUEST_READ",
        .on_read_ready    = request_read_ready,
    },
    {
        .state            = REQUEST_FORWARD,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_WRITE,
        .description      = "REQUEST_FORWARD",
        .on_write_ready   = request_forward_ready,
    },
    {
        .state            = RESPONSE_READ,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_READ,
        .rst_buffer       = READ_BUFFER | WRITE_BUFFER,
        .description      = "RESPONSE_READ",
        .on_read_ready    = response_read_ready,
    },
    {
        .state            = RESPONSE_FORWARD,
        .client_interest  = OP_WRITE,
        .target_interest  = OP_NOOP,
        .description      = "RESPONSE_FORWARD",
        .on_write_ready   = response_forward_ready,
    },
    {
        .state            = CONNECT_RESPONSE,
        .client_interest  = OP_WRITE,
        .target_interest  = OP_NOOP,
        .description      = "CONNECT_RESPONSE",
        .on_write_ready   = connect_response_ready,
    },
    {
        .state            = TCP_TUNNEL,
        .client_interest  = OP_READ,
        .target_interest  = OP_READ,
        .description      = "TCP_TUNNEL",
        .on_read_ready    = tcp_tunnel_read_ready,
        .on_write_ready   = tcp_tunnel_forward_ready,
    },
    {
        .state            = CLIENT_CLOSE_CONNECTION,
        .client_interest  = OP_READ,
        .target_interest  = OP_WRITE,
        .description      = "CLIENT_CLOSE_CONNECTION",
        .on_arrival       = client_close_connection_arrival
    },
    {
        .state            = TARGET_CLOSE_CONNECTION,
        .client_interest  = OP_WRITE,
        .target_interest  = OP_READ,
        .description      = "TARGET_CLOSE_CONNECTION",
        .on_arrival       = target_close_connection_arrival
    },
    {
        .state            = END,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_NOOP,
        .description      = "END",
        .on_arrival       = end_arrival
    },
    {
        .state            = ERROR_STATE,
        .client_interest  = OP_WRITE,
        .target_interest  = OP_NOOP,
        .description      = "ERROR_STATE",
        .on_write_ready   = error_write_ready
    }
};


state_machine proto_stm = {
    .initial = REQUEST_READ,
    .states = state_defs,
    .max_state = ERROR_STATE
};

#define log_error(_description) \
    log(ERROR, "At state %d: %s", key->item->stm.current->state, _description);

/* -------------------------------------- HANDLERS IMPLEMENTATIONS -------------------------------------- */

static unsigned request_read_ready(struct selector_key *key) {

    key->item->last_activity = time(NULL);

    if (! buffer_can_write(&(key->item->read_buffer))) {
        log_error("Read buffer limit reached")
        return notify_error(key, PAYLOAD_TOO_LARGE, REQUEST_READ);
    }

    // Read request bytes into read buffer

    size_t space;
    uint8_t * raw_req = buffer_write_ptr(&(key->item->read_buffer), &space);
    ssize_t readBytes = read(key->item->client_socket, raw_req, space);

    if(readBytes < 0) {
        if(errno != EBADF && errno != SIGPIPE)
            log_error("Failed to read from client");
        return CLIENT_CLOSE_CONNECTION;
    }

    if (readBytes <= 0)
        return CLIENT_CLOSE_CONNECTION;

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %ld bytes from socket %d", readBytes, key->item->client_socket);

    // Process the request

    return process_request(key);

}


static unsigned request_forward_ready(struct selector_key *key) {

    if (! buffer_can_read(&(key->item->write_buffer)))
        return REQUEST_FORWARD;

    // Read request bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->item->target_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != SIGPIPE)
            log_error("Failed to write request to target");
        return TARGET_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->item->target_socket);

    if ((size_t) sentBytes < size)
        return REQUEST_FORWARD;

    return RESPONSE_READ;

}


static unsigned response_read_ready(struct selector_key *key) {

    if (! buffer_can_write(&(key->item->read_buffer))) {
        log_error("Read buffer limit reached")
        return notify_error(key, BAD_GATEWAY, REQUEST_READ);
    }

    // Read response bytes into read buffer

    size_t space;
    uint8_t * raw_res = buffer_write_ptr(&(key->item->read_buffer), &space);
    ssize_t readBytes = read(key->item->target_socket, raw_res, space);

    if(readBytes < 0) {
        if(errno != EBADF && errno != SIGPIPE)
            log_error("Failed to read response from target");
        return TARGET_CLOSE_CONNECTION;
    }

    if (readBytes <= 0)
        return TARGET_CLOSE_CONNECTION;

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %ld bytes from socket %d", readBytes, key->item->target_socket);

    // Parse the response

    // TODO: Add parser and handle pending message case

    // Write processed response bytes into write buffer

    uint8_t * raw_res_proc = buffer_write_ptr(&(key->item->write_buffer), &space);
    strncpy((char *) raw_res_proc, (char *) raw_res, readBytes);
    buffer_write_adv(&(key->item->write_buffer), readBytes);

    return RESPONSE_FORWARD;

}


static unsigned response_forward_ready(struct selector_key *key) {

    if (! buffer_can_read(&(key->item->write_buffer)))
        return RESPONSE_FORWARD;

    // Read response bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->item->client_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != SIGPIPE)
            log_error("Failed to write response to client");
        return CLIENT_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->item->client_socket);

    if ((size_t) sentBytes < size)
        return RESPONSE_FORWARD;

    return REQUEST_READ;

}


static unsigned connect_response_ready(struct selector_key *key) {

    if (! buffer_can_read(&(key->item->write_buffer)))
        return CONNECT_RESPONSE;

    // Read response bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->item->client_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != SIGPIPE)
            log_error("Failed to write connect response to client");
        return END;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->item->client_socket);

    if ((size_t) sentBytes < size)
        return CONNECT_RESPONSE;

    return TCP_TUNNEL;

}


static unsigned tcp_tunnel_read_ready(struct selector_key *key) {

    // On this context: read_buffer = client_buffer, write_buffer = target_buffer

    // Choose peer socket buffer for writing to it

    int peer_fd = key->active_fd == key->item->client_socket
        ? key->item->target_socket : key->item->client_socket;

    buffer * buffer = peer_fd == key->item->client_socket
        ? &(key->item->read_buffer) : &(key->item->write_buffer);

    // If the buffer is full wait for it to be consumed

    if (! buffer_can_write(buffer))
        return TCP_TUNNEL;

    // Copy active socket bytes into peer socket buffer

    size_t space;
    uint8_t * ptr = buffer_write_ptr(buffer, &space);
    ssize_t readBytes = read(key->active_fd, ptr, space);

    if (readBytes < 0) {
        if(errno != EBADF && errno != SIGPIPE)
            log_error("Failed to read from active socket");
        return END;
    }

    if (readBytes == 0)
        return END;

    buffer_write_adv(buffer, readBytes);

    log(DEBUG, "Received %ld bytes from socket %d", readBytes, key->active_fd);

    // Declare interest on writing to peer and return

    if (key->active_fd == key->item->client_socket)
        key->item->target_interest |= OP_WRITE;
    else
        key->item->client_interest |= OP_WRITE;

    selector_update_fdset(key->s, key->item);

    return TCP_TUNNEL;

}


static unsigned tcp_tunnel_forward_ready(struct selector_key *key) {

    // On this context: read_buffer = client_buffer, write_buffer = target_buffer

    // Choose active socket buffer for reading from it

    buffer * buffer = key->active_fd == key->item->client_socket
        ? &(key->item->read_buffer) : &(key->item->write_buffer);

    // If the buffer is empty wait for it to be filled

    if (! buffer_can_read(buffer))
        return TCP_TUNNEL;

    // Copy bytes from peer socket buffer to active socket

    size_t size;
    uint8_t *ptr = buffer_read_ptr(buffer, &size);
    ssize_t sentBytes = write(key->active_fd, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != SIGPIPE)
            log_error("Failed to write to active socket");
        return END;
    }

    buffer_read_adv(buffer, sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->active_fd);

     // If write is over turn off interest on writing on active socket, then return

    if (((size_t) sentBytes) == size) {
        if (key->active_fd == key->item->client_socket)
            key->item->client_interest &= ~OP_WRITE;
        else
            key->item->target_interest &= ~OP_WRITE;

        selector_update_fdset(key->s, key->item);
    }

    return TCP_TUNNEL;

}


static unsigned error_write_ready(struct selector_key *key) {

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->item->client_socket, ptr, size);

    log(ERROR, "Read %ld bytes from write buffer", sentBytes);

    if (sentBytes < 0) {
        log(ERROR, "Failed to notify error to client");
        return END;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    if ((size_t) sentBytes < size)
        return ERROR_STATE;

    #pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
    return (unsigned) key->item->data;

}


static unsigned client_close_connection_arrival(const unsigned state, struct selector_key *key) {

    log(DEBUG, "Client closed connection from socket %d", key->item->client_socket);

    // Read last bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->item->target_socket, ptr, size);

    if (sentBytes < 0)
        return END;

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->item->target_socket);

    if ((size_t) sentBytes < size)
        return CLIENT_CLOSE_CONNECTION;

    return END;

}


static unsigned target_close_connection_arrival(const unsigned state, struct selector_key *key) {

    log(DEBUG, "Target Closed connection from socket %d", key->item->target_socket);

    // Read last bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->item->client_socket, ptr, size);

    if (sentBytes < 0)
        return END;

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->item->client_socket);

    if ((size_t) sentBytes < size)
        return TARGET_CLOSE_CONNECTION;

    return END;

}


static unsigned end_arrival(const unsigned state, struct selector_key *key){

    item_kill(key->s, key->item);

    return END;
    
}


/* -------------------------------------- AUXILIARS IMPLEMENTATIONS -------------------------------------- */

static unsigned notify_error(struct selector_key *key, int status_code, unsigned next_state) {

    struct response res = { .status_code = status_code };
    char * raw_res = create_response(&res);

    size_t space;
    char * ptr = (char *) buffer_write_ptr(&(key->item->write_buffer), &space);

    strncpy(ptr, raw_res, space);
    buffer_write_adv(&(key->item->write_buffer), strlen(ptr));

    #pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
    key->item->data = (void *) next_state;

    return ERROR_STATE;

}


static unsigned process_request(struct selector_key * key) {

    // Instantiate a request struct or use previous one if it exists

    if(key->item->pdata.request == NULL)
        key->item->pdata.request = calloc(1, sizeof(struct request));
    
    struct request * request = key->item->pdata.request;

    // Parse the request and check for pending and failure cases

    parse_state parser_state = http_parser_parse(
        &(key->item->read_buffer), request, &(key->item->pdata)
    );

    if (parser_state == PENDING)
        return REQUEST_READ;

    if (parser_state == FAILED) {
        free(key->item->pdata.request);
        key->item->pdata.request = NULL;

        log_error("Invalid request received");
        return notify_error(key, BAD_REQUEST, REQUEST_READ);
    }

    // Parse the request target URL

    struct url url;
    parse_url(request->url, &url);

    if (strlen(request->url) == 0) {
        free(key->item->pdata.request);
        key->item->pdata.request = NULL;

        return notify_error(key, BAD_REQUEST, REQUEST_READ);
    }

    // Establish connection to target

    unsigned ret = connect_target(key, url.hostname, url.port);
    if (ret == ERROR_STATE) {
        free(key->item->pdata.request);
        key->item->pdata.request = NULL;
        return ret;
    }
        
    if (request->method == CONNECT) {

        // The request method is CONNECT, a response shall be sent
        // and TCP tunnel be established
        
        // Write response bytes into write buffer

        struct response res = { .status_code = RESPONSE_OK };
        char * raw_res = create_response(&res);

        size_t space;
        uint8_t * ptr = buffer_write_ptr(&(key->item->write_buffer), &space);

        strcpy((char *) ptr, raw_res);
        buffer_write_adv(&(key->item->write_buffer), strlen(raw_res));

        // Go to send response state

        free(key->item->pdata.request);
        key->item->pdata.request = NULL;

        return CONNECT_RESPONSE;

    } else {

        // The request method is a traditional one, request shall be proccessed
        // and then forwarded

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

        // Go to forward request state

        free(key->item->pdata.request);
        key->item->pdata.request = NULL;

        return REQUEST_FORWARD;

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
        return notify_error(key, FORBIDDEN, REQUEST_READ);
    }

    // Check that target is not the proxy itself

    if (strcmp(target_host, "localhost") == 0 && target_port == proxy_conf.proxyArgs.proxy_port) {
        log(INFO, "Rejected connection to proxy itself");
        return notify_error(key, FORBIDDEN, REQUEST_READ);
    }

    // Open connection with target

    int targetSocket = create_tcp_client(target_host, target_port);
    if (targetSocket < 0) {
        log_error("Failed to connect to target");
        return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);
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

    if (close_detected) {
        log(DEBUG, "Should close connection");
    }

    raw_req = create_request(req);
    free(raw_req);

}
