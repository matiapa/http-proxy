#include <stm.h>
#include <logger.h>
#include <string.h>

/* -------------------------------------- PROXY STATES -------------------------------------- */

enum proxy_state {
    /*
     * Recieves 'CONNECT' message from client
     *
     * Interests:
     *   - Client: OP_READ
     *
     * Transitions:
     *   - CONNECT_READ     While message is not ready
     *   - CONNECT_WRITE    When message is over
     *   - ERROR            Parsing/IO error
    */
    CONNECT_READ,

    /*
     * Resolves target FQDN and connects to target server
     *
     * Interests:
     *   None
     *
     * Transitions:
     *   - CONNECT_WRITE    If connection went right
     *   - ERROR            If connection went wrong
    */
    CONNECT_CONNECTING,

    /*
     * Sends CONNECT response message to client
     *
     * Interests:
     *   - Client: OP_WRITE
     *
     * Transitions:
     *   - CONNECT_WRITE    While message is not ready
     *   - REQUEST_READ     When message is over
     *   - ERROR            Parsing/IO error
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
     *   - ERROR            Parsing/IO error
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
     *   - ERROR            IO error
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
     *   - ERROR            Parsing/IO error
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
     *   - ERROR            IO error
    */
    RESPONSE_WRITE,

    CLOSE_CONNECTION,

    ERROR,
};


/* -------------------------------------- HANDLERS PROTOTYPES -------------------------------------- */

/* ------------------------------------------------------------
  Reads connection requests from client.
------------------------------------------------------------ */
static unsigned connect_read_ready(const unsigned state, struct selector_key *key);

/* ------------------------------------------------------------
  Sends connection response to client.
------------------------------------------------------------ */
static unsigned connect_write_ready(const unsigned state, struct selector_key *key);

/* ------------------------------------------------------------
  Reads HTTP requests from client.
------------------------------------------------------------ */
static unsigned request_read_ready(const unsigned state, struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP requests to target.
------------------------------------------------------------ */
static unsigned request_write_ready(const unsigned state, struct selector_key *key);

/* ------------------------------------------------------------
  Reads HTTP responses from target.
------------------------------------------------------------ */
static unsigned response_read_ready(const unsigned state, struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP responses to client.
------------------------------------------------------------ */
static unsigned response_write_ready(const unsigned state, struct selector_key *key);

/* ------------------------------------------------------------
  Closes the client and associated target connections.
------------------------------------------------------------ */
static unsigned close_connection_arrival(const unsigned state, struct selector_key *key);

/* ------------------------------------------------------------
  Handles proxy errors.
------------------------------------------------------------ */
static unsigned error_arrival(const unsigned state, struct selector_key *key);


/* -------------------------------------- STATE MACHINE DEFINITION -------------------------------------- */


static const struct state_definition state_defs[] = {
    {
        .state            = CONNECT_READ,
        .client_interest  = OP_READ,
        .target_interest  = OP_NOOP,
        .on_read_ready    = connect_read_ready,
    },
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
        .on_read_ready    = response_read_ready,
    },
    {
        .state            = RESPONSE_WRITE,
        .client_interest  = OP_WRITE,
        .target_interest  = OP_NOOP,
        .on_write_ready   = response_write_ready,
    },
    {
        .state            = CLOSE_CONNECTION,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_NOOP,
        .on_arrival       = close_connection_arrival
    },
    {
        .state            = ERROR,
        .client_interest  = OP_WRITE,
        .on_arrival       = error_arrival,
        // TODO: .on_write_ready  (ERROR NOTIFICATION)
    }
};


/* -------------------------------------- HANDLERS IMPLEMENTATIONS -------------------------------------- */


static unsigned connect_read_ready(const unsigned state, struct selector_key *key) {

    if (! buffer_can_write(&(key->item->read_buffer)))
        return CONNECT_READ;

    // Read request bytes into read buffer

    size_t space;
    uint8_t *ptr = buffer_write_ptr(&(key->item->read_buffer), &space);
    int readBytes = read(key->src_socket, ptr, space);

    if(readBytes < 0) {
        key->item->data = "Failed to read from src socket";
        return ERROR;
    }

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %d bytes from socket %d\n", readBytes, key->src_socket);

    // Parse the request

    // TODO: Add parser and handle pending message case

    if (readBytes <= 0)
        return CLOSE_CONNECTION;

    if(strcmp(ptr, "CONNECT") != 0) {
        key->item->data = "Failed to parse message";
        return ERROR;
    }

    // Open connection with target socket

    int targetSocket = setupClientSocket("localhost", key->item->data);
    if (targetSocket < 0) {
        key->item->data = "Failed to connect to target";
        return ERROR;
    }

    key->item->target_socket = targetSocket;

    // Write response bytes into write buffer

    char *response = "Connected!";

    ptr = buffer_write_ptr(&(key->item->write_buffer), &space);
    strcpy(ptr, response);
    buffer_write_adv(&(key->item->write_buffer), strlen(response));

    return CONNECT_WRITE;

}


static unsigned connect_write_ready(const unsigned state, struct selector_key *key) {

    if (! buffer_can_read(&(key->item->write_buffer)))
        return CONNECT_WRITE;

    // Read response bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    int sentBytes = write(key->dst_socket, ptr, size);

    if (sentBytes < 0) {
        key->item->data = "Failed to write to dst socket";
        return ERROR;
    }

    buffer_write_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %d bytes to socket %d\n", sentBytes, key->dst_socket);

    if (sentBytes < size)
        return CONNECT_WRITE;

    // Clean the buffers go to next step

    buffer_reset(&(key->item->read_buffer));
    buffer_reset(&(key->item->write_buffer));

    return REQUEST_READ;

}


static unsigned request_read_ready(const unsigned state, struct selector_key *key) {

    if (! buffer_can_write(&(key->item->read_buffer)))
        return REQUEST_READ;

    // Read request bytes into read buffer

    size_t space;
    uint8_t * ptr = buffer_write_ptr(&(key->item->read_buffer), &space);
    int readBytes = read(key->src_socket, ptr, space);

    if(readBytes < 0) {
        key->item->data = "Failed to read from src socket";
        return ERROR;
    }

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %d bytes from socket %d\n", readBytes, key->src_socket);

    // Parse the request

    // TODO: Add parser and handle pending message case

    // TODO: Add via header

    if (readBytes <= 0)
        return CLOSE_CONNECTION;

    char * raw_req_proc[1024];
    memcpy(raw_req_proc, ptr, 1024);

    // Write processed request bytes into write buffer

    ptr = buffer_write_ptr(&(key->item->write_buffer), &space);
    strncpy(raw_req_proc, ptr, 1024);
    buffer_write_adv(&(key->item->write_buffer), 1024);

    return REQUEST_WRITE;

}


static unsigned request_write_ready(const unsigned state, struct selector_key *key) {

    // Read request bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    int sentBytes = write(key->dst_socket, ptr, size);

    if (sentBytes < 0) {
        key->item->data = "Failed to write to dst socket";
        return ERROR;
    }

    buffer_write_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %d bytes to socket %d\n", sentBytes, key->dst_socket);

    if (sentBytes < size)
        return REQUEST_WRITE;

    return RESPONSE_READ;

}


static unsigned response_read_ready(const unsigned state, struct selector_key *key) {

    if (! buffer_can_write(&(key->item->read_buffer)))
        return RESPONSE_READ;

    // Read response bytes into read buffer

    size_t space;
    uint8_t * ptr = buffer_write_ptr(&(key->item->read_buffer), &space);
    int readBytes = read(key->dst_socket, ptr, space);

    if(readBytes < 0) {
        key->item->data = "Failed to read from dst socket";
        return ERROR;
    }

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %d bytes from socket %d\n", readBytes, key->dst_socket);

    // Parse the response

    // TODO: Add parser and handle pending message case

    if (readBytes <= 0)
        return CLOSE_CONNECTION;

    char * raw_res_proc[1024];
    memcpy(raw_res_proc, ptr, 1024);

    // Write processed response bytes into write buffer

    ptr = buffer_write_ptr(&(key->item->write_buffer), &space);
    strncpy(raw_res_proc, ptr, 1024);
    buffer_write_adv(&(key->item->write_buffer), 1024);

    return RESPONSE_WRITE;

}


static unsigned response_write_ready(const unsigned state, struct selector_key *key) {

    // Read response bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    int sentBytes = write(key->src_socket, ptr, size);

    if (sentBytes < 0) {
        key->item->data = "Failed to write to src socket";
        return ERROR;
    }

    buffer_write_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %d bytes to socket %d\n", sentBytes, key->src_socket);

    if (sentBytes < size)
        return RESPONSE_WRITE;

    // Clean the buffers and start over

    buffer_reset(&(key->item->read_buffer));
    buffer_reset(&(key->item->write_buffer));

    return REQUEST_READ;

}