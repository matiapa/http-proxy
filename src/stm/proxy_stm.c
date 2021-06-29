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
#include <statistics.h>
#include <dissector.h>
#include <arpa/inet.h>
#include <proxy_stm.h>

#pragma GCC diagnostic ignored "-Wunused-parameter"


/* -------------------------------------- HANDLERS PROTOTYPES -------------------------------------- */


/* ------------------------------------------------------------
  Reads TCP traffic from client or target. 
------------------------------------------------------------ */
unsigned tcp_tunnel_read_ready(unsigned int state, selector_key_t *key);

/* ------------------------------------------------------------
  Forwards TCP traffic to corresponding peer.
------------------------------------------------------------ */
unsigned tcp_tunnel_forward_ready(unsigned int state, selector_key_t *key);

/* ------------------------------------------------------------
  Sends last messages from client to target then closes connection
------------------------------------------------------------ */
unsigned client_close_connection_arrival(const unsigned state, selector_key_t *key);

/* ------------------------------------------------------------
  Sends last messages from target to client then closes connection
------------------------------------------------------------ */
unsigned target_close_connection_arrival(const unsigned state, selector_key_t *key);

/* ------------------------------------------------------------
  Trap state handler
------------------------------------------------------------ */
unsigned end_arrival(const unsigned state, selector_key_t *key);

/* ------------------------------------------------------------
  Notifies proxy errors.
------------------------------------------------------------ */
unsigned error_write_ready(unsigned int state, selector_key_t *key);


/* -------------------------------------- AUXILIARS PROTOTYPES -------------------------------------- */


/* ------------------------------------------------------------
  Writes error information on buffer and returns error state.
------------------------------------------------------------ */
unsigned notify_error(selector_key_t *key, int status_code, unsigned next_state);


/* -------------------------------------- STATE MACHINE DEFINITION -------------------------------------- */


const struct state_definition state_defs[] = {
    {
        .state            = REQUEST_READ,
        .client_interest  = OP_READ,
        .target_interest  = OP_NOOP,
        .rst_buffer       = READ_BUFFER | WRITE_BUFFER,
        .description      = "REQUEST_READ",
        .on_read_ready    = request_read_ready,
    },
    {
        .state            = DOH_CONNECT,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_WRITE,
        .description      = "DOH_CONNECT",
        .on_write_ready   = doh_connect_write_ready,
    },
    {
        .state            = RESPONSE_DOH,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_READ,
        .description      = "RESPONSE_DOH",
        .on_read_ready    = response_doh_read_ready,
    },
    {
        .state            = TRY_IPS,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_NOOP,
        .description      = "TRY_IPS",
        .on_arrival       = try_ips_arrival,
    },
    {
        .state            = REQUEST_CONNECT,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_WRITE,
        .description      = "REQUEST_CONNECT",
        .on_write_ready   = request_connect_write_ready
    },
    {
        .state            = REQUEST_FORWARD,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_WRITE,
        .description      = "REQUEST_FORWARD",
        .on_write_ready   = request_forward_ready,
    },
    {
        .state            = REQ_BODY_READ,
        .client_interest  = OP_READ,
        .target_interest  = OP_NOOP,
        .description      = "REQ_BODY_READ",
        .on_read_ready    = req_body_read_ready,
    },
    {
        .state            = REQ_BODY_FORWARD,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_WRITE,
        .description      = "REQ_BODY_FORWARD",
        .on_write_ready   = req_body_forward_ready,
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
        .state            = RES_BODY_READ,
        .client_interest  = OP_NOOP,
        .target_interest  = OP_READ,
        .description      = "RES_BODY_READ",
        .on_read_ready    = res_body_read_ready,
    },
    {
        .state            = RES_BODY_FORWARD,
        .client_interest  = OP_WRITE,
        .target_interest  = OP_NOOP,
        .description      = "RES_BODY_FORWARD",
        .on_write_ready   = res_body_forward_ready,
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


/* -------------------------------------- HANDLERS IMPLEMENTATIONS -------------------------------------- */


unsigned tcp_tunnel_read_ready(unsigned int state, selector_key_t *key) {

    // On this context: read_buffer = client_buffer, write_buffer = target_buffer

    // Choose peer socket buffer for writing to it

    int peer_fd = key->fd == I(key)->client_socket
        ? I(key)->target_socket : I(key)->client_socket;

    buffer * buffer = peer_fd == I(key)->client_socket
        ? &(I(key)->read_buffer) : &(I(key)->write_buffer);

    // If the buffer is full wait for it to be consumed

    if (! buffer_can_write(buffer)) {
        if (key->fd == I(key)->client_socket)
            selector_set_interest(key->s, I(key)->client_socket, OP_NOOP);
        else
            selector_set_interest(key->s, I(key)->target_socket, OP_NOOP);
        return TCP_TUNNEL;
    }

    // Copy active socket bytes into peer socket buffer

    size_t space;
    uint8_t * ptr = buffer_write_ptr(buffer, &space);
    ssize_t readBytes = read(key->fd, ptr, space);

    if (readBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to read from active socket");
        return END;
    }

    if (readBytes == 0)
        return END;

    buffer_write_adv(buffer, readBytes);

    log(DEBUG, "Received %lu bytes from socket %d", (size_t) readBytes, key->fd);

    struct buffer aux_buffer;
    memcpy(&aux_buffer, buffer, sizeof(struct buffer));

    if (proxy_conf.disectorsEnabled) {
        pop3_state state = pop3_parse(&aux_buffer, &(I(key)->pop3_parser));

        if (state == POP3_SUCCESS) {
            if (I(key)->pop3_parser.user [0]!= 0 && I(key)->pop3_parser.pass [0]!= 0) {
                log(DEBUG, "User: %s", I(key)->pop3_parser.user);
                log(DEBUG, "Pass: %s", I(key)->pop3_parser.pass);
                print_credentials(
                    POP3,I(key)->last_target_url.hostname, I(key)->last_target_url.port,
                    I(key)->pop3_parser.user, I(key)->pop3_parser.pass
                );
                memset(I(key)->pop3_parser.user, 0, MAX_USER_LENGTH);
                memset(I(key)->pop3_parser.pass, 0, MAX_PASS_LENGTH);
            }            
        }
    }

    // Calculate statistics

    add_bytes_recieved(readBytes);

    // Declare interest on writing to peer and return

    if (key->fd == I(key)->client_socket)
        selector_set_interest(key->s, I(key)->target_socket, OP_WRITE);
    else
        selector_set_interest(key->s, I(key)->client_socket, OP_WRITE);

    return TCP_TUNNEL;
}


unsigned tcp_tunnel_forward_ready(unsigned int state, selector_key_t *key) {

    // On this context: read_buffer = client_buffer, write_buffer = target_buffer

    // Choose active socket buffer for reading from it

    buffer * buffer = key->fd == I(key)->client_socket
        ? &(I(key)->read_buffer) : &(I(key)->write_buffer);

    // If the buffer is empty wait for it to be filled

    if (! buffer_can_read(buffer))
        return TCP_TUNNEL;

    // Copy bytes from peer socket buffer to active socket

    size_t size;
    uint8_t *ptr = buffer_read_ptr(buffer, &size);
    ssize_t sentBytes = write(key->fd, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write to active socket");
        return END;
    }

    buffer_read_adv(buffer, sentBytes);

    log(DEBUG, "Sent %lu bytes to socket %d", (size_t) sentBytes, key->fd);

    //statistics
    add_sent_bytes(sentBytes);

    // If write is over turn off interest on writing on active socket, then return

    if (((size_t) sentBytes) == size) {
        if (key->fd == I(key)->client_socket)
            selector_set_interest(key->s, I(key)->client_socket, OP_NOOP);
        else
            selector_set_interest(key->s, I(key)->target_socket, OP_NOOP);
    }

    selector_set_interest(key->s, I(key)->client_socket, OP_READ);
    selector_set_interest(key->s, I(key)->client_socket, OP_READ);

    return TCP_TUNNEL;

}


unsigned error_write_ready(unsigned int state, selector_key_t *key) {

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(I(key)->write_buffer), &size);
    ssize_t sentBytes = write(I(key)->client_socket, ptr, size);

    if (sentBytes < 0) {
        log(ERROR, "Failed to notify error to client");
        return END;
    }

    buffer_read_adv(&(I(key)->write_buffer), sentBytes);

    //statistics
    add_sent_bytes(sentBytes);

    if ((size_t) sentBytes < size)
        return ERROR_STATE;

    #pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
    return (unsigned) I(key)->data;

}


unsigned client_close_connection_arrival(const unsigned state, selector_key_t *key) {

    log(DEBUG, "Client closed connection from socket %d", I(key)->client_socket);

    // Read last bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(I(key)->write_buffer), &size);
    ssize_t sentBytes = write(I(key)->target_socket, ptr, size);

    if (sentBytes < 0){
        return END;
    }

    buffer_read_adv(&(I(key)->write_buffer), sentBytes);

    log(DEBUG, "Sent %lu bytes to socket %d", (size_t) sentBytes, I(key)->target_socket);

    //statistics
    add_sent_bytes(sentBytes);

    if ((size_t) sentBytes < size)
        return CLIENT_CLOSE_CONNECTION;

    return END;

}


unsigned target_close_connection_arrival(const unsigned state, selector_key_t *key) {

    log(DEBUG, "Target Closed connection from socket %d", I(key)->target_socket);

    // Read last bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(I(key)->read_buffer), &size);
    ssize_t sentBytes = write(I(key)->client_socket, ptr, size);

    if (sentBytes < 0){
        return END;
    }
    buffer_read_adv(&(I(key)->read_buffer), sentBytes);

    log(DEBUG, "Sent %lu bytes to socket %d", (size_t) sentBytes, I(key)->client_socket);
    
    //statistics
    add_sent_bytes(sentBytes);


    if ((size_t) sentBytes < size)
        return TARGET_CLOSE_CONNECTION;

    return END;

}


unsigned end_arrival(const unsigned state, selector_key_t *key){

    // TODO: Handle properly
    //item_kill(key->s, I(key));
    remove_conection();
    return END;
    
}


/* -------------------------------------- AUXILIARS IMPLEMENTATIONS -------------------------------------- */


unsigned notify_error(selector_key_t *key, int status_code, unsigned next_state) {
    
    print_Access(inet_ntoa(I(key)->client.sin_addr),ntohs(I(key)->client.sin_port), I(key)->req_parser.request.url,  I(key)->req_parser.request.method,status_code);
    http_request_parser_reset(&(I(key)->req_parser));
    http_response_parser_reset(&(I(key)->res_parser));

    size_t space;
    char * ptr = (char *) buffer_write_ptr(&(I(key)->write_buffer), &space);

    http_response res = { .status = status_code };
    int written = write_response(&res, ptr, space, false);

    buffer_write_adv(&(I(key)->write_buffer), written);

    #pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
    I(key)->data = (void *) next_state;

    return ERROR_STATE;

}
