#include <logger.h>
#include <errno.h>
#include <string.h>
#include <buffer.h>
#include <stm.h>
#include <selector.h>
#include <client.h>
#include <proxy_stm.h>

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
     *   - ERROR_STATE            Parsing/IO error
    */
    CONNECT_READ,
    
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
  Reads connection requests from client.
------------------------------------------------------------ */
static unsigned connect_read_ready(struct selector_key *key);

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
        .on_arrival       = error_arrival,
        // TODO: .on_write_ready  (ERROR NOTIFICATION)
    }
};


state_machine proto_stm = {
    .initial = CONNECT_READ,
    .states = state_defs,
    .max_state = ERROR_STATE
};


/* -------------------------------------- HANDLERS IMPLEMENTATIONS -------------------------------------- */


static unsigned connect_read_ready(struct selector_key *key) {

    if (! buffer_can_write(&(key->item->read_buffer)))
        return CONNECT_READ;

    // Read request bytes into read buffer

    size_t space;
    uint8_t *ptr = buffer_write_ptr(&(key->item->read_buffer), &space);
    ssize_t readBytes = read(key->src_socket, ptr, space);

    if(readBytes < 0) {
        key->item->data = "Failed to read from src socket";
        return ERROR_STATE;
    }

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %ld bytes from socket %d", readBytes, key->src_socket);

    // Parse the request

    // TODO: Add parser and handle pending message case

    if (readBytes <= 0)
        return END;

    if(strcmp((char *) ptr, "CONNECT\n") != 0) {
        key->item->data = "Failed to parse message";
        return ERROR_STATE;
    }

    // Open connection with target socket

    int targetSocket = setupClientSocket("localhost", "8081");
    if (targetSocket < 0) {
        key->item->data = "Failed to connect to target";
        return ERROR_STATE;
    }

    key->item->target_socket = targetSocket;

    // Write response bytes into write buffer

    char *response = "Connected!";

    ptr = buffer_write_ptr(&(key->item->write_buffer), &space);
    strcpy((char *) ptr, response);
    buffer_write_adv(&(key->item->write_buffer), strlen(response));

    return CONNECT_WRITE;

}


static unsigned connect_write_ready(struct selector_key *key) {

    if (! buffer_can_read(&(key->item->write_buffer)))
        return CONNECT_WRITE;

    // Read response bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->src_socket, ptr, size);

    if (sentBytes < 0) {
        switch(errno) {
            case EBADF:
                return END;
                break;
            default:
                 key->item->data = "Failed to write to src socket";
                return ERROR_STATE;
        }
       
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->src_socket);

    if ((size_t) sentBytes < size)
        return CONNECT_WRITE;

    // Clean the buffers go to next step

    buffer_reset(&(key->item->read_buffer));
    buffer_reset(&(key->item->write_buffer));

    return REQUEST_READ;

}


static unsigned request_read_ready(struct selector_key *key) {

    if (! buffer_can_write(&(key->item->read_buffer)))
        return REQUEST_READ;

    // Read request bytes into read buffer

    size_t space;
    uint8_t * raw_req = buffer_write_ptr(&(key->item->read_buffer), &space);
    ssize_t readBytes = read(key->src_socket, raw_req, space);

    if(readBytes < 0) {
        key->item->data = "Failed to read from src socket";
        return ERROR_STATE;
    }

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %ld bytes from socket %d", readBytes, key->src_socket);

    // Parse the request

    // TODO: Add parser and handle pending message case

    // TODO: Add via header

    if (readBytes <= 0)
        return CLIENT_CLOSE_CONNECTION;

    // Write processed request bytes into write buffer

    // TODO: Check for maximum request size and throw error if necessary

    uint8_t * raw_req_proc = buffer_write_ptr(&(key->item->write_buffer), &space);
    strncpy((char *) raw_req_proc, (char *) raw_req, readBytes);
    buffer_write_adv(&(key->item->write_buffer), readBytes);

    return REQUEST_WRITE;

}


static unsigned request_write_ready(struct selector_key *key) {

    // Read request bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->dst_socket, ptr, size);

    if (sentBytes < 0) {
        switch(errno) {
            case EBADF:
                return TARGET_CLOSE_CONNECTION;
                break;
            default:
                key->item->data = "Failed to write to dst socket";
                return ERROR_STATE;
        }
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->dst_socket);

    if ((size_t) sentBytes < size)
        return REQUEST_WRITE;

    return RESPONSE_READ;

}


static unsigned response_read_ready(struct selector_key *key) {

    if (! buffer_can_write(&(key->item->read_buffer)))
        return RESPONSE_READ;

    // Read response bytes into read buffer

    size_t space;
    uint8_t * raw_res = buffer_write_ptr(&(key->item->read_buffer), &space);
    ssize_t readBytes = read(key->dst_socket, raw_res, space);

    if(readBytes < 0) {
        key->item->data = "Failed to read from dst socket";
        return ERROR_STATE;
    }

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %ld bytes from socket %d", readBytes, key->dst_socket);

    // Parse the response

    // TODO: Add parser and handle pending message case

    if (readBytes <= 0)
        return TARGET_CLOSE_CONNECTION;

    // Write processed response bytes into write buffer

    // TODO: Check for maximum response size and throw error if necessary

    uint8_t * raw_res_proc = buffer_write_ptr(&(key->item->write_buffer), &space);
    strncpy((char *) raw_res_proc, (char *) raw_res, readBytes);
    buffer_write_adv(&(key->item->write_buffer), readBytes);

    return RESPONSE_WRITE;

}


static unsigned response_write_ready(struct selector_key *key) {

    // Read response bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->src_socket, ptr, size);

    if (sentBytes < 0) {
        switch(errno) {
            case EBADF:
                return CLIENT_CLOSE_CONNECTION;
                break;
            default:
                key->item->data = "Failed to write to src socket";
                return ERROR_STATE;
        }
        
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->src_socket);

    if ((size_t) sentBytes < size)
        return RESPONSE_WRITE;

    // Clean the buffers and start over

    buffer_reset(&(key->item->read_buffer));
    buffer_reset(&(key->item->write_buffer));

    return REQUEST_READ;

}


#pragma GCC diagnostic ignored "-Wunused-parameter"
static unsigned error_arrival(const unsigned state, struct selector_key *key) {

    log(ERROR, "%s", (char *) key->item->data);

    return ERROR_STATE;   // TODO: Pass as data if error is recoverable
}


static unsigned client_close_connection_arrival(const unsigned state, struct selector_key *key) {
   log(DEBUG, "Client Closed connection from socket %d", key->src_socket);

    // Read last bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->dst_socket, ptr, size);

    if (sentBytes < 0) {
        return END;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->dst_socket);

    if ((size_t) sentBytes < size)
        return CLIENT_CLOSE_CONNECTION;

    return END;
}

static unsigned target_close_connection_arrival(const unsigned state, struct selector_key *key) {
// Read last bytes from write buffer

    log(DEBUG, "Target Closed connection from socket %d", key->dst_socket);
    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->src_socket, ptr, size);

    if (sentBytes < 0) {
        return END;
    }

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