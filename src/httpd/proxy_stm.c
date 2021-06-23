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
#include <base64.h>
#include <dissector.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <doh_client.h>
#include <arpa/inet.h>

// Many of the state transition handlers don't use the state param so we are ignoring this warning
#pragma GCC diagnostic ignored "-Wunused-parameter"

/* -------------------------------------- PROXY STATES -------------------------------------- */

/// TODO: Update comments for this enums

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
     *   - REQUEST_CONNECT      When request message is over
     *   - ERROR_STATE          Parsing/IO error
    */
    REQUEST_READ,

    /*
     * Waits for the connection to DoH server to complete
     *
     * Interests:
     *   - Client: OP_NOOP
     *   - Target: OP_WRITE
     *
     * Transitions:
     *   - RESPONSE_DOH         Connection completed and request sent
     *   - ERROR_STATE          IO error
    */
    DOH_CONNECT,

    /*
     * Recieves a response from DoH server
     *
     * Interests:
     *   - Client: OP_NOOP
     *   - Target (DoH): OP_READ
     *
     * Transitions:
     *   - RESPONSE_DOH         While response message is not over
     *   - TRY_IPS              When request message is over
     *   - ERROR_STATE          Parsing/IO error
    */
    RESPONSE_DOH,

    /*
     * Tries to connnect to a resolved target IP
     *
     * Interests:
     *   - Client: OP_NOOP
     *   - Target: OP_NOOP
     *
     * Transitions:
     *   - REQUEST_CONNECT      Initialized a connection to a target IP.
     *   - RESPONSE_DOH         No IPv6s left to try. Sent IPv4 DoH request.
     *   - ERROR_STATE          No IPv4s left to try. Game over.
    */
    TRY_IPS,

    /*
     * Waits for the connection to target to complete
     *
     * Interests:
     *   - Client: OP_NOOP
     *   - Target: OP_WRITE
     *
     * Transitions:
     *   - REQUEST_FORWARD      When connection is completed
     *   - ERROR_STATE          IO error
    */
    REQUEST_CONNECT,

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

    REQ_BODY_READ,

    REQ_BODY_FORWARD,

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

    RES_BODY_READ,

    RES_BODY_FORWARD,

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
  Reads HTTP requests message part from client.
------------------------------------------------------------ */
static unsigned request_read_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Handles the completion of connection to DoH.
------------------------------------------------------------ */
static unsigned doh_connect_write_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Reads DoH response from server.
------------------------------------------------------------ */
static unsigned response_doh_read_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Tries to connect to a target resolved IP.
------------------------------------------------------------ */
static unsigned try_ips_arrival(const unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Handles the completion of connection to target.
------------------------------------------------------------ */
static unsigned request_connect_write_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP requests message part to target.
------------------------------------------------------------ */
static unsigned request_forward_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Reads HTTP requests body part from client.
------------------------------------------------------------ */
static unsigned req_body_read_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP requests body part to target.
------------------------------------------------------------ */
static unsigned req_body_forward_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Reads HTTP responses message part from target.
------------------------------------------------------------ */
static unsigned response_read_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP responses message part to client.
------------------------------------------------------------ */
static unsigned response_forward_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Reads HTTP responses body part from target.
------------------------------------------------------------ */
static unsigned res_body_read_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP responses body part to client.
------------------------------------------------------------ */
static unsigned res_body_forward_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Sends connection response to client.
------------------------------------------------------------ */
static unsigned connect_response_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Reads TCP traffic from client or target. 
------------------------------------------------------------ */
static unsigned tcp_tunnel_read_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Forwards TCP traffic to corresponding peer.
------------------------------------------------------------ */
static unsigned tcp_tunnel_forward_ready(unsigned int state, struct selector_key *key);

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
static unsigned error_write_ready(unsigned int state, struct selector_key *key);


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
  Processes request headers according to RFC 7230 specs.
------------------------------------------------------------ */
static void process_request_headers(http_request * req, char * target_host, char * proxy_host);

/* ------------------------------------------------------------
  Processes request headers and deposits user:password into raw authorization.
------------------------------------------------------------ */
static int extract_http_credentials(http_request * request);

/* ------------------------------------------------------------
  Processes an HTTP response and returns next state.
------------------------------------------------------------ */
static unsigned process_response(struct selector_key * key);

/* ------------------------------------------------------------
  Processes response headers according to RFC 7230 specs.
------------------------------------------------------------ */
static void process_response_headers(http_response * res, char * proxy_host);


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


/* -------------------------------------- MACROS DEFINITIONS -------------------------------------- */

#define log_error(_description) \
    log(ERROR, "At state %u: %s", key->item->stm.current->state, _description);

#define remove_array_elem(array, pos, size) \
    memcpy(array+pos, array+pos+1, size-pos-1)

#define rtrim(s) \
    char* back = s + strlen(s); \
    while(isspace(*--back)); \
    *(back+1) = 0;

#define ADDR_BUFFER_SIZE 1024


/* -------------------------------------- HANDLERS IMPLEMENTATIONS -------------------------------------- */

static unsigned request_read_ready(unsigned int state, struct selector_key *key) {

    key->item->last_activity = time(NULL);

    if (! buffer_can_write(&(key->item->read_buffer))) {
        log_error("Read buffer limit reached")
        return notify_error(key, PAYLOAD_TOO_LARGE, REQUEST_READ);
    }

    // Read request bytes into read buffer

    size_t space;
    uint8_t * raw_req = buffer_write_ptr(&(key->item->read_buffer), &space);
    ssize_t readBytes = read(key->item->client_socket, raw_req, space);

    if(readBytes <= 0) {
        if(readBytes < 0 && errno != EBADF && errno != EPIPE)
            log_error("Failed to read from client");
        return CLIENT_CLOSE_CONNECTION;
    }

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %lu bytes from socket %d", (size_t) readBytes, key->item->client_socket);

    // Calculate statistics

    add_bytes_recieved(readBytes);   

    // Process the request

    return process_request(key);

}


static unsigned doh_connect_write_ready(unsigned int state, struct selector_key *key) {

    int socket_error;
    socklen_t socket_error_len = sizeof(socket_error);
    int sock_opt = getsockopt(key->item->doh.server_socket, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len);
    if(sock_opt != 0){
        log(ERROR, "DoH server getsockopt(%d) failed", key->item->doh.server_socket)
        perror("reason");
    }

    if(socket_error != 0){
        log(ERROR, "Failed to connect to DoH server")
        return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);
    }

    if (send_doh_request(key, key->item->doh.family) < 0) {
        return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);
    }
    
    return RESPONSE_DOH;

}


static unsigned response_doh_read_ready(unsigned int state, struct selector_key *key) {

    int ans_count = doh_client_read(key);
    if (ans_count == -1) {

        log(ERROR, "Failed to read response from DoH server")
        return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);

    } else if (ans_count == 0) {
        if (key->item->doh.family == AF_INET) {
            free(key->item->doh.target_address_list);
            key->item->doh.family = AF_INET6;

            if (send_doh_request(key, key->item->doh.family) < 0)
                return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);
            return RESPONSE_DOH;
        } else {
            doh_kill(key);
            log(ERROR, "Connecting to target")

            return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);
        }
    } else { // Salio todo joya
        return TRY_IPS;
    }

}


static unsigned try_ips_arrival(const unsigned int state, struct selector_key *key) {
    char addrBuffer[ADDR_BUFFER_SIZE];

    struct addrinfo * current_addr = key->item->doh.current_target_addr;
    struct addrinfo * address_list = key->item->doh.target_address_list;

    // Try to connect to an address
    int target_socket = -1;
    while (current_addr != NULL && target_socket == -1) {
        if (is_proxy_host(current_addr->ai_addr) && key->item->doh.url.port == proxy_conf.proxyArgs.proxy_port) {
            log(INFO, "Prevented proxy loop")
            free(address_list);
            return notify_error(key, FORBIDDEN, REQUEST_READ);
        }

        target_socket = socket(current_addr->ai_family, current_addr->ai_socktype, current_addr->ai_protocol);

        selector_fd_set_nio(target_socket);

        if (target_socket < 0) {
            sockaddr_print(current_addr->ai_addr, addrBuffer);
            log(DEBUG, "Can't create target socket on %s", addrBuffer) 
            current_addr = current_addr->ai_next;
            continue;
        }

        if (connect(target_socket, current_addr->ai_addr, current_addr->ai_addrlen) == -1) {
            if (errno == EINPROGRESS) {
                current_addr = current_addr->ai_next;
                break;
            } else {
                current_addr = current_addr->ai_next;
                continue;
            }
        } else {
            abort(); // Such a thing can't happen!
        }
    }

    // Release address resource

    key->item->doh.current_target_addr = current_addr;

    if (target_socket < 0) {
        if (key->item->doh.family == AF_INET) {
            free(address_list);
            key->item->doh.family = AF_INET6;
            key->item->target_socket = key->item->doh.server_socket; 
            // Esto último es provisional, por compatibilidad con los permisos de la STM

            if (send_doh_request(key, key->item->doh.family) < 0)
                return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);

            return RESPONSE_DOH;
        } else {
            doh_kill(key);
            log(ERROR, "Connecting to target")
            return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);
        }
    } else {
        key->item->target_socket = target_socket;
        return REQUEST_CONNECT;
    }
}


static unsigned request_connect_write_ready(unsigned int state, struct selector_key *key) {

    int socket_error;
    socklen_t socket_error_len = sizeof(socket_error);
    int sock_opt = getsockopt(key->item->target_socket, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len);
    if (sock_opt != 0) {
        log(ERROR, "Target getsockopt(%d) failed", key->item->target_socket)
        perror("reason");
    }

    if (socket_error != 0) {
        close(key->item->target_socket);
        if (key->item->doh.server_socket > 0)
            return TRY_IPS;
        else
            return notify_error(key, BAD_GATEWAY, REQUEST_READ);
    } else { // la conexión funciono
        memcpy(&(key->item->last_target_url), &(key->item->doh.url), sizeof(struct url));
        if (key->item->doh.server_socket > 0)
            doh_kill(key);
        // int aux = key->item->doh.target_socket;
        // key->item->target_socket = aux;
    }

    // Update last connection

    http_request * request = &(key->item->req_parser.request);

    // Check for request method

    if (request->method == CONNECT) {

        // The request method is CONNECT, a response shall be sent
        // and TCP tunnel be established
        
        // Write response bytes into write buffer

        size_t space;
        char * ptr = (char *) buffer_write_ptr(&(key->item->write_buffer), &space);

        http_response res = { .status = RESPONSE_OK };
        int written = write_response(&res, ptr, space, false);

        buffer_write_adv(&(key->item->write_buffer), written);

        print_Access(inet_ntoa(key->item->client.sin_addr), ntohs(key->item->client.sin_port), key->item->req_parser.request.url, key->item->req_parser.request.method, 200);

        // Go to send response state

        return CONNECT_RESPONSE;

    } else { 

        // The request method is a traditional one, request shall be proccessed
        // and then forwarded
        
        if (request->method == OPTIONS && strlen(key->item->last_target_url.path) == 0)
            sprintf(request->url, "*");

        // Process request headers

        char proxy_hostname[VIA_PROXY_NAME_SIZE] = {0};
        if(strlen(proxy_conf.viaProxyName) > 0) {
            strncpy(proxy_hostname, proxy_conf.viaProxyName, VIA_PROXY_NAME_SIZE);
        } else {
            get_machine_fqdn(proxy_hostname);
        }
        
        process_request_headers(request, key->item->last_target_url.hostname, proxy_hostname);

        // Extract credentials if present

        if (proxy_conf.disectorsEnabled)
            extract_http_credentials(request);

        // Write processed request bytes into write buffer

        size_t space;
        char * ptr = (char *) buffer_write_ptr(&(key->item->write_buffer), &space);

        int written = write_request(request, ptr, space, false);

        buffer_write_adv(&(key->item->write_buffer), written);

        // Go to forward request state

        return REQUEST_FORWARD;

    }
}


static unsigned request_forward_ready(unsigned int state, struct selector_key *key) {

    if (! buffer_can_read(&(key->item->write_buffer)))
        return REQUEST_FORWARD;

    // Read request bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->item->target_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write request to target");
        return TARGET_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %lu bytes to socket %d", (size_t) sentBytes, key->item->target_socket);

    // Calculate statistics

    add_sent_bytes(sentBytes);

    if ((size_t) sentBytes < size)
        return REQUEST_FORWARD;
 
    if (key->item->req_parser.request.message.body_length == 0) {
        return RESPONSE_READ;
    } else if (buffer_can_read(&(key->item->read_buffer))) {
        // Body present, read some bytes
        size_t remaining;
        buffer_read_ptr(&(key->item->read_buffer), &remaining);
        key->item->req_parser.message_parser.current_body_length += remaining;
        return REQ_BODY_FORWARD;
    } else {
        // Body present, didn't read bytes
        return key->item->req_parser.request.message.hasExpect
            ? TCP_TUNNEL : REQ_BODY_READ;
    }

}


static unsigned req_body_read_ready(unsigned int state, struct selector_key *key) {

    key->item->last_activity = time(NULL);

    if (! buffer_can_write(&(key->item->read_buffer)))
        return REQ_BODY_FORWARD;

    // Read body bytes into read buffer

    size_t space;
    uint8_t * body = buffer_write_ptr(&(key->item->read_buffer), &space);
    ssize_t readBytes = read(key->item->client_socket, body, space);

    if(readBytes <= 0) {
        if(readBytes < 0 && errno != EBADF && errno != EPIPE)
            log_error("Failed to read from client");
        return CLIENT_CLOSE_CONNECTION;
    }

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %lu body bytes from socket %d", (size_t) readBytes, key->item->client_socket);

    // Calculate statistics

    add_bytes_recieved(readBytes);

    http_request_parser * rp = &(key->item->req_parser);

    if ((rp->message_parser.current_body_length += readBytes) < rp->request.message.body_length)
        return REQ_BODY_READ;
    else
        return REQ_BODY_FORWARD;

}


static unsigned req_body_forward_ready(unsigned int state, struct selector_key *key) {

    // Will read the content directly from read buffer

    // Read body bytes from read buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->read_buffer), &size);
    ssize_t sentBytes = write(key->item->target_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write body to target");
        return TARGET_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(key->item->read_buffer), sentBytes);

    log(DEBUG, "Sent %lu body bytes to socket %d", (size_t) sentBytes, key->item->target_socket);

    // Calculate statistics

    add_sent_bytes(sentBytes);

    http_request_parser * rp = &(key->item->req_parser);

    if ((size_t) sentBytes < size)
        return REQ_BODY_FORWARD;
    else if ((rp->message_parser.current_body_length) < rp->request.message.body_length)
        return REQ_BODY_READ;
    else
        return RESPONSE_READ;

}


static unsigned response_read_ready(unsigned int state, struct selector_key *key) {

    if (! buffer_can_write(&(key->item->read_buffer))) {
        log_error("Read buffer limit reached")
        return notify_error(key, BAD_GATEWAY, REQUEST_READ);
    }

    // Read response bytes into read buffer

    size_t space;
    uint8_t * raw_res = buffer_write_ptr(&(key->item->read_buffer), &space);
    ssize_t readBytes = read(key->item->target_socket, raw_res, space);

    if(readBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to read response from target");
        return TARGET_CLOSE_CONNECTION;
    }

    if (readBytes == 0)
        return TARGET_CLOSE_CONNECTION;

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %lu bytes from socket %d", (size_t) readBytes, key->item->target_socket);

    // Calculate statistics

    add_bytes_recieved(readBytes);

    // Process the response

    return process_response(key);

}


static unsigned response_forward_ready(unsigned int state, struct selector_key *key) {

    // Read response bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->item->client_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write response to client");
        return CLIENT_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %lu bytes to socket %d", (size_t) sentBytes, key->item->client_socket);

    // Calculate statistics

    add_sent_bytes(sentBytes);

    if ((size_t) sentBytes < size)
        return RESPONSE_FORWARD;

       
    if (key->item->res_parser.response.message.body_length == 0) {
        // No body
        http_request_parser_reset(&(key->item->req_parser));
        http_response_parser_reset(&(key->item->res_parser));
        return REQUEST_READ;
    } else if (buffer_can_read(&(key->item->read_buffer))) {
        // Body present, read some bytes
        size_t remaining;
        buffer_read_ptr(&(key->item->read_buffer), &remaining);
        key->item->res_parser.message_parser.current_body_length += remaining;
        return RES_BODY_FORWARD;
    } else {
        // Body present, didn't read bytes
        return RES_BODY_READ;
    }
    

}


static unsigned res_body_read_ready(unsigned int state, struct selector_key *key) {

    key->item->last_activity = time(NULL);

    if (! buffer_can_write(&(key->item->read_buffer)))
        return RES_BODY_FORWARD;

    // Read body bytes into read buffer

    size_t space;
    uint8_t * body = buffer_write_ptr(&(key->item->read_buffer), &space);
    ssize_t readBytes = read(key->item->target_socket, body, space);

    if(readBytes <= 0) {
        if(readBytes < 0 && errno != EBADF && errno != EPIPE)
            log_error("Failed to read from target");
        return TARGET_CLOSE_CONNECTION;
    }

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %lu body bytes from socket %d", (size_t) readBytes, key->item->target_socket);

    // Calculate statistics

    add_bytes_recieved(readBytes);

    http_response_parser * rp = &(key->item->res_parser);

    rp->message_parser.current_body_length += readBytes;

    if ((rp->message_parser.current_body_length) < rp->response.message.body_length)
        return RES_BODY_READ;
    else
        return RES_BODY_FORWARD;

}


static unsigned res_body_forward_ready(unsigned int state, struct selector_key *key) {

    // Will read the content directly from read buffer

    // Read body bytes from read buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->read_buffer), &size);
    ssize_t sentBytes = write(key->item->client_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write body to client");
        return CLIENT_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(key->item->read_buffer), sentBytes);

    log(DEBUG, "Sent %lu body bytes to socket %d", (size_t) sentBytes, key->item->client_socket);

    // Calculate statistics

    add_sent_bytes(sentBytes);

    http_response_parser * rp = &(key->item->res_parser);

    if ((size_t) sentBytes < size) {
        return RES_BODY_FORWARD;
    } else if ((rp->message_parser.current_body_length) < rp->response.message.body_length) {
        return RES_BODY_READ;
    } else {
        http_request_parser_reset(&(key->item->req_parser));
        http_response_parser_reset(&(key->item->res_parser));
        return REQUEST_READ;
    }

}


static unsigned connect_response_ready(unsigned int state, struct selector_key *key) {

    if (! buffer_can_read(&(key->item->write_buffer)))
        return CONNECT_RESPONSE;

    // write response bytes to socket

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->item->client_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write connect response to client");
        return END;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %lu bytes to socket %d", (size_t) sentBytes, key->item->client_socket);

    //statistics
    add_sent_bytes(sentBytes);

    if ((size_t) sentBytes < size)
        return CONNECT_RESPONSE;

    memset(&(key->item->pop3_parser), 0, sizeof(pop3_parser_data));
    pop3_parser_init(&(key->item->pop3_parser));

    return TCP_TUNNEL;

}


static unsigned tcp_tunnel_read_ready(unsigned int state, struct selector_key *key) {

    // On this context: read_buffer = client_buffer, write_buffer = target_buffer

    // Choose peer socket buffer for writing to it

    int peer_fd = key->active_fd == key->item->client_socket
        ? key->item->target_socket : key->item->client_socket;

    buffer * buffer = peer_fd == key->item->client_socket
        ? &(key->item->read_buffer) : &(key->item->write_buffer);

    // If the buffer is full wait for it to be consumed

    if (! buffer_can_write(buffer)) {
        if (key->active_fd == key->item->client_socket)
            key->item->client_interest &= ~OP_READ;
        else
            key->item->target_interest &= ~OP_READ;
        return TCP_TUNNEL;
    }

    // Copy active socket bytes into peer socket buffer

    size_t space;
    uint8_t * ptr = buffer_write_ptr(buffer, &space);
    ssize_t readBytes = read(key->active_fd, ptr, space);

    if (readBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to read from active socket");
        return END;
    }

    if (readBytes == 0)
        return END;

    buffer_write_adv(buffer, readBytes);

    log(DEBUG, "Received %lu bytes from socket %d", (size_t) readBytes, key->active_fd);

    struct buffer aux_buffer;
    memcpy(&aux_buffer, buffer, sizeof(struct buffer));

    if (proxy_conf.disectorsEnabled) {
        pop3_state state = pop3_parse(&aux_buffer, &(key->item->pop3_parser));

        if (state == POP3_SUCCESS) {
            if (key->item->pop3_parser.user [0]!= 0 && key->item->pop3_parser.pass [0]!= 0) {
                log(DEBUG, "User: %s", key->item->pop3_parser.user);
                log(DEBUG, "Pass: %s", key->item->pop3_parser.pass);
                print_credentials(
                    POP3,key->item->last_target_url.hostname, key->item->last_target_url.port,
                    key->item->pop3_parser.user, key->item->pop3_parser.pass
                );
                memset(key->item->pop3_parser.user, 0, MAX_USER_LENGTH);
                memset(key->item->pop3_parser.pass, 0, MAX_PASS_LENGTH);
            }            
        }
    }

    // Calculate statistics

    add_bytes_recieved(readBytes);

    // Declare interest on writing to peer and return

    if (key->active_fd == key->item->client_socket)
        key->item->target_interest |= OP_WRITE;
    else
        key->item->client_interest |= OP_WRITE;

    selector_update_fdset(key->s, key->item);

    return TCP_TUNNEL;
}


static unsigned tcp_tunnel_forward_ready(unsigned int state, struct selector_key *key) {

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
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write to active socket");
        return END;
    }

    buffer_read_adv(buffer, sentBytes);

    log(DEBUG, "Sent %lu bytes to socket %d", (size_t) sentBytes, key->active_fd);

    //statistics
    add_sent_bytes(sentBytes);

    // If write is over turn off interest on writing on active socket, then return

    if (((size_t) sentBytes) == size) {
        if (key->active_fd == key->item->client_socket)
            key->item->client_interest &= ~OP_WRITE;
        else
            key->item->target_interest &= ~OP_WRITE;

        selector_update_fdset(key->s, key->item);
    }

    key->item->client_interest |= OP_READ;
    key->item->target_interest |= OP_READ;

    return TCP_TUNNEL;

}


static unsigned error_write_ready(unsigned int state, struct selector_key *key) {

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->item->client_socket, ptr, size);

    if (sentBytes < 0) {
        log(ERROR, "Failed to notify error to client");
        return END;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    //statistics
    add_sent_bytes(sentBytes);

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

    if (sentBytes < 0){
        return END;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %lu bytes to socket %d", (size_t) sentBytes, key->item->target_socket);

    //statistics
    add_sent_bytes(sentBytes);

    if ((size_t) sentBytes < size)
        return CLIENT_CLOSE_CONNECTION;

    return END;

}


static unsigned target_close_connection_arrival(const unsigned state, struct selector_key *key) {

    log(DEBUG, "Target Closed connection from socket %d", key->item->target_socket);

    // Read last bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(key->item->read_buffer), &size);
    ssize_t sentBytes = write(key->item->client_socket, ptr, size);

    if (sentBytes < 0){
        return END;
    }
    buffer_read_adv(&(key->item->read_buffer), sentBytes);

    log(DEBUG, "Sent %lu bytes to socket %d", (size_t) sentBytes, key->item->client_socket);
    
    //statistics
    add_sent_bytes(sentBytes);


    if ((size_t) sentBytes < size)
        return TARGET_CLOSE_CONNECTION;

    return END;

}


static unsigned end_arrival(const unsigned state, struct selector_key *key){

    item_kill(key->s, key->item);
    remove_conection();
    return END;
    
}


/* -------------------------------------- AUXILIARS IMPLEMENTATIONS -------------------------------------- */

static unsigned notify_error(struct selector_key *key, int status_code, unsigned next_state) {
    
    print_Access(inet_ntoa(key->item->client.sin_addr),ntohs(key->item->client.sin_port), key->item->req_parser.request.url,  key->item->req_parser.request.method,status_code);
    http_request_parser_reset(&(key->item->req_parser));
    http_response_parser_reset(&(key->item->res_parser));

    size_t space;
    char * ptr = (char *) buffer_write_ptr(&(key->item->write_buffer), &space);

    http_response res = { .status = status_code };
    int written = write_response(&res, ptr, space, false);

    buffer_write_adv(&(key->item->write_buffer), written);

    #pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
    key->item->data = (void *) next_state;

    return ERROR_STATE;

}


static unsigned process_request(struct selector_key * key) {

    // Parse the request and check for pending and failure cases

    parse_state parser_state = http_request_parser_parse(
        &(key->item->req_parser), &(key->item->read_buffer)
    );

    if (parser_state == PENDING)
        return REQUEST_READ;
        
    if (parser_state == FAILED)
        return notify_error(key, key->item->req_parser.error_code, REQUEST_READ);

    http_request * request = &(key->item->req_parser.request);

    // Parse the request target URL

    memset(&(key->item->doh), 0, sizeof(struct doh_client));
    int r = parse_url(request->url, &(key->item->doh.url));
    // TODO: Move parsed URL to another structure

    if (r < 0)
        return notify_error(key, BAD_REQUEST, REQUEST_READ);

    if (strlen(request->url) == 0)
        return notify_error(key, BAD_REQUEST, REQUEST_READ);

    if (request->method == TRACE)
        return notify_error(key, METHOD_NOT_ALLOWED, REQUEST_READ);

    // Log the access of the client

    log_client_access(key->item->client_socket, request->url);

    // If there is an established connection close it
    // TODO: Close the connection on a previous stage

    if (key->item->target_socket > 0) {
        close(key->item->target_socket);
    }

    // Prepare to connect to new target

    log(DEBUG, "Connection requested to %s:%d", key->item->doh.url.hostname, key->item->doh.url.port);

    // Check that target is not blacklisted

    if (strstr(proxy_conf.targetBlacklist, key->item->doh.url.hostname) != NULL) {
        log(INFO, "Rejected connection to %s due to target blacklist", key->item->doh.url.hostname);
        return notify_error(key, FORBIDDEN, REQUEST_READ);
    }

    // Prepare to resolve target address

    /*--------- Chequeo si el target esta en formato IP o es Localhost ---------*/
    struct addrinfo * addrinfo;
    if (resolve_string(&(addrinfo), key->item->doh.url.hostname, key->item->doh.url.port) >= 0) {

        if (is_proxy_host(addrinfo->ai_addr) && key->item->doh.url.port == proxy_conf.proxyArgs.proxy_port) {
            log(INFO, "Prevented proxy loop")
            free(addrinfo);
            return notify_error(key, FORBIDDEN, REQUEST_READ);
        }

        int sock = socket(addrinfo->ai_family, addrinfo->ai_socktype, addrinfo->ai_protocol);

        selector_fd_set_nio(sock);

        if (sock < 0) 
            return notify_error(key, BAD_GATEWAY, REQUEST_READ);

        if (connect(sock, addrinfo->ai_addr, addrinfo->ai_addrlen) == -1) {
            if (errno == EINPROGRESS) {
                key->item->target_socket = sock;
                free(addrinfo);
                return REQUEST_CONNECT;
            } else {
                free(addrinfo);
                return notify_error(key, BAD_GATEWAY, REQUEST_READ);
            }
        } else {
            abort(); // Such a thing can't happen!
        }

    } else {
        if (doh_client_init(key) < 0) {
            return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);
        }
    }

    return DOH_CONNECT;
    
}


static void process_request_headers(http_request * req, char * target_host, char * proxy_host) {

    bool replaced_host_header = false;
    bool replaced_via_header = false;
    bool close_detected = false;

    for (size_t i=0; i < req->message.header_count; i++) {

        // Right trim header names

        rtrim(req->message.headers[i][0]);

        // Replace Host header if target hostname is not empty

        if (strcmp(req->message.headers[i][0], "Host") == 0 && strlen(target_host) > 0) {
            strcpy(req->message.headers[i][1], " ");
            strcpy(req->message.headers[i][1] + 1, target_host);
            replaced_host_header = true;
        }

        // Replace Via header appending proxy hostname

        if (strcmp(req->message.headers[i][0], "Via") == 0) {
            char new_token[128];
            sprintf(new_token, ", 1.1 %s", proxy_host);

            strcat(req->message.headers[i][1], new_token);
            
            replaced_via_header = true;
        }

        // Remove headers listed on Connection header

        if (strcmp(req->message.headers[i][0], "Connection") == 0) {
            char * connection_headers = req->message.headers[i][1];

            if (strstr(connection_headers, "Close"))
                close_detected = true;

            for(size_t j=0; j < req->message.header_count; j++) {
                if (strstr(connection_headers, req->message.headers[j][0])) {
                    remove_array_elem(req->message.headers, j, req->message.header_count);
                    req->message.header_count -= 1;
                }
            }
        }

    }

    // If a Host header was not present but a hostname was given, add it

    if(!replaced_host_header && strlen(target_host) > 0) {
        req->message.header_count += 1;
        strcpy(req->message.headers[req->message.header_count - 1][0], "Host");
        strcpy(req->message.headers[req->message.header_count - 1][1], " ");
        strcpy(req->message.headers[req->message.header_count - 1][1] + 1, target_host);
    }

    // If a Via header was not present, add it

    if(!replaced_via_header) {
        req->message.header_count += 1;
        sprintf(req->message.headers[req->message.header_count - 1][0], "Via");
        sprintf(req->message.headers[req->message.header_count - 1][1], " 1.1 %s", proxy_host);
    }

    /// TODO: Handle close detected

    if (close_detected) {
        log(DEBUG, "Should close connection");
    }

}


static int extract_http_credentials(http_request * request) {

    char raw_authorization[HEADER_LENGTH] = {0};
    int found=0;

    for (size_t i = 0; i < request->message.header_count&&!found; i++){
        if (strcmp(request->message.headers[i][0],"Authorization")==0){
            strncpy(raw_authorization,request->message.headers[i][1], HEADER_LENGTH);
            int k=0;
            while (isspace(raw_authorization[k]))
            {
                k++;
            }
            
            if(strncmp(&raw_authorization[k],"Basic ",6)==0){
                // strcpy(raw_authorization,&raw_authorization[7]);
                //max header length minus the chars in "Basic "
                for (int j = 0; j < (HEADER_LENGTH-6-k); j++)
                {
                    raw_authorization[j]=raw_authorization[j+6+k];
                }
                found=1;
            }
        }
    }
    
    if (found) {
        
        char user[64];
        char pass[64];
        log(DEBUG,"encoded authorization is %s",raw_authorization);
        int length=0;
        unsigned char * user_pass=unbase64( raw_authorization, strlen(raw_authorization), &length );
        log(DEBUG,"unencoded authorization is %s",user_pass);
        user_pass[length]=0;
        int j=0;
        for (int i = 0; i < length; i++)
        {
            if (user_pass[i]==':'){
                user[i]=0;
                j=i+1;
                break;
            }else
                user[i]=user_pass[i]; 
        }
        strcpy(pass,(char*)&user_pass[j]);
        
        struct url url;
        parse_url(request->url, &url);
        print_credentials(HTTP,url.hostname, url.port,user,pass);
    }
    

    return found;
}


static unsigned process_response(struct selector_key * key) {

    // Parse the response and check for pending and failure cases

    bool ignore_length = key->item->req_parser.request.method == HEAD ? true : false;

    parse_state parser_state = http_response_parser_parse(
        &(key->item->res_parser), &(key->item->read_buffer), ignore_length
    );

    if (parser_state == PENDING)
        return RESPONSE_READ;

    if (parser_state == FAILED) {
        return notify_error(key, BAD_GATEWAY, REQUEST_READ);
    }

    print_Access(inet_ntoa(key->item->client.sin_addr), ntohs(key->item->client.sin_port), key->item->req_parser.request.url, 
    key->item->req_parser.request.method, key->item->res_parser.response.status);

    http_response * response = &(key->item->res_parser.response);

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
    char * ptr = (char *) buffer_write_ptr(&(key->item->write_buffer), &space);

    int written = write_response(response, ptr, space, false);

    buffer_write_adv(&(key->item->write_buffer), written);

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
