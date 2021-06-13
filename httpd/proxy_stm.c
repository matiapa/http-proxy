#include <logger.h>
#include <errno.h>
#include <string.h>
#include <buffer.h>
#include <stm.h>
#include <selector.h>
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
  Processes request headers according to RFC 7230 specs.
------------------------------------------------------------ */
static void process_request_headers(http_request * req, char * target_host, char * proxy_host);

/* ------------------------------------------------------------
  Initiates a connection to target and returns next state.
------------------------------------------------------------ */
static unsigned connect_target(struct selector_key * key, char * target, int port);

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
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to read from client");
        return CLIENT_CLOSE_CONNECTION;
    }

    if (readBytes <= 0)
        return CLIENT_CLOSE_CONNECTION;

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %ld bytes from socket %d", readBytes, key->item->client_socket);

    // Calculate statistics

    add_bytes_recieved(readBytes);   

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
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write request to target");
        return TARGET_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->item->target_socket);

    if ((size_t) sentBytes < size)
        return REQUEST_FORWARD;

    //statistics
    add_sent_bytes(sentBytes);
 
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
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to read response from target");
        return TARGET_CLOSE_CONNECTION;
    }

    if (readBytes == 0)
        return TARGET_CLOSE_CONNECTION;

    buffer_write_adv(&(key->item->read_buffer), readBytes);

    log(DEBUG, "Received %ld bytes from socket %d", readBytes, key->item->target_socket);

    // Calculate statistics

    add_bytes_recieved(readBytes);

    // Process the response

    return process_response(key);

}


static unsigned response_forward_ready(struct selector_key *key) {

    if (! buffer_can_read(&(key->item->write_buffer)))
        return RESPONSE_FORWARD;

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

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->item->client_socket);

    //statistics
    add_sent_bytes(sentBytes);

    if ((size_t) sentBytes < size)
        return RESPONSE_FORWARD;

    return REQUEST_READ;

}


static unsigned connect_response_ready(struct selector_key *key) {

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

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->item->client_socket);

    //statistics
    add_sent_bytes(sentBytes);

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
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to read from active socket");
        return END;
    }

    if (readBytes == 0)
        return END;

    buffer_write_adv(buffer, readBytes);

    log(DEBUG, "Received %ld bytes from socket %d", readBytes, key->active_fd);

    struct buffer aux_buffer;
    memcpy(&aux_buffer, buffer, sizeof(struct buffer));

    pop3_state state = pop3_parse(&aux_buffer, &(key->item->pop3_parser));

    if (state == POP3_SUCCESS) {
        if (key->item->pop3_parser.user != NULL) {
            log(DEBUG, "User: %s", key->item->pop3_parser.user);
            key->item->pop3_parser.user[key->item->pop3_parser.user_len] = '\n';
        }
        if (key->item->pop3_parser.pass != NULL) {
            log(DEBUG, "Pass: %s", key->item->pop3_parser.pass);
            key->item->pop3_parser.pass[key->item->pop3_parser.pass_len] = '\n';
        }
        pop3_parser_reset(&(key->item->pop3_parser));
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
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write to active socket");
        return END;
    }

    buffer_read_adv(buffer, sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->active_fd);

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

    if (sentBytes < 0)
        return END;

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->item->target_socket);

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
    uint8_t *ptr = buffer_read_ptr(&(key->item->write_buffer), &size);
    ssize_t sentBytes = write(key->item->client_socket, ptr, size);

    if (sentBytes < 0)
        return END;

    buffer_read_adv(&(key->item->write_buffer), sentBytes);

    log(DEBUG, "Sent %ld bytes to socket %d", sentBytes, key->item->client_socket);
    
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

#define RESET_REQUEST() \
    http_request_parser_reset(&(key->item->req_parser)); \
    free(key->item->req_parser.request); \
    key->item->req_parser.request = NULL;

#define RESET_RESPONSE() \
    http_response_parser_reset(&(key->item->res_parser)); \
    free(key->item->res_parser.response); \
    key->item->res_parser.response = NULL;

#define remove_array_elem(array, pos, size) \
    memcpy(array+pos, array+pos+1, size-pos-1)

#define rtrim(s) \
    char* back = s + strlen(s); \
    while(isspace(*--back)); \
    *(back+1) = 0;


static unsigned notify_error(struct selector_key *key, int status_code, unsigned next_state) {

    size_t space;
    char * ptr = (char *) buffer_write_ptr(&(key->item->write_buffer), &space);

    http_response res = { .status = status_code };
    int written = write_response(&res, ptr, space);

    buffer_write_adv(&(key->item->write_buffer), written);

    #pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
    key->item->data = (void *) next_state;

    return ERROR_STATE;

}


static unsigned process_request(struct selector_key * key) {

    // Instantiate a request struct or use previous one if it exists

    if(key->item->req_parser.request == NULL)
        key->item->req_parser.request = calloc(1, sizeof(http_request));
    
    http_request * request = key->item->req_parser.request;

    // Parse the request and check for pending and failure cases

    parse_state parser_state = http_request_parser_parse(
        &(key->item->req_parser), &(key->item->read_buffer), request
    );

    if (parser_state == PENDING)
        return REQUEST_READ;

    if (parser_state == FAILED) {
        RESET_REQUEST();
        return notify_error(key, key->item->req_parser.error_code, REQUEST_READ);
    }

    // Parse the request target URL

    struct url url;
    parse_url(request->url, &url);

    if (strlen(request->url) == 0) {
        RESET_REQUEST();
        return notify_error(key, key->item->req_parser.error_code, REQUEST_READ);
    }

    log_client_access(key->item->client_socket, request->url);

    // Establish connection to target

    unsigned ret = connect_target(key, url.hostname, url.port);
    if (ret == ERROR_STATE) {
        RESET_REQUEST();
        return ret;
    }
        
    if (request->method == CONNECT) {

        // The request method is CONNECT, a response shall be sent
        // and TCP tunnel be established
        
        // Write response bytes into write buffer

        size_t space;
        char * ptr = (char *) buffer_write_ptr(&(key->item->write_buffer), &space);

        http_response res = { .status = RESPONSE_OK };
        int written = write_response(&res, ptr, space);

        buffer_write_adv(&(key->item->write_buffer), written);

        // Go to send response state

        RESET_REQUEST();

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

        // Extract credentials if present

        if (proxy_conf.disectorsEnabled)
            extract_http_credentials(request);

        // Write processed request bytes into write buffer

        size_t space;
        char * ptr = (char *) buffer_write_ptr(&(key->item->write_buffer), &space);

        int written = write_request(request, ptr, space);

        buffer_write_adv(&(key->item->write_buffer), written);

        // Go to forward request state

        RESET_REQUEST();

        return REQUEST_FORWARD;

    }
    
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

    // TODO: Handle close detected

    if (close_detected) {
        log(DEBUG, "Should close connection");
    }

}


static int extract_http_credentials(http_request * request) {
    // TODO: guardar la user_pass

    char raw_authorization[HEADER_LENGTH];
    int found=0;

    for (size_t i = 0; i < request->message.header_count&&!found; i++){
        if (strcmp(request->message.headers[i][0],"Authorization")==0){
            strncpy(raw_authorization,request->message.headers[i][1],HEADER_LENGTH);
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


static unsigned process_response(struct selector_key * key) {

    // Instantiate a response struct or use previous one if it exists

    if(key->item->res_parser.response == NULL)
        key->item->res_parser.response = calloc(1, sizeof(http_response));

    http_response * response = key->item->res_parser.response;

    // Parse the response and check for pending and failure cases

    parse_state parser_state = http_response_parser_parse(
        &(key->item->res_parser), &(key->item->read_buffer), response
    );

    if (parser_state == PENDING)
        return RESPONSE_READ;

    if (parser_state == FAILED) {
        RESET_RESPONSE();
        return notify_error(key, BAD_GATEWAY, REQUEST_READ);
    }

    // Process response headers

    char proxy_hostname[128];
    if(strlen(proxy_conf.viaProxyName) > 0) {
        strncpy(proxy_hostname, proxy_conf.viaProxyName, 128);
    } else {
        get_machine_fqdn(proxy_hostname);
    }

    process_response_headers(response, proxy_hostname);

    // Write processed response bytes into write buffer

    size_t space;
    char * ptr = (char *) buffer_write_ptr(&(key->item->write_buffer), &space);

    int written = write_response(response, ptr, space);

    buffer_write_adv(&(key->item->write_buffer), written);

    // Go to forward response state

    RESET_RESPONSE();

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

    // TODO: Handle close detected

    if (close_detected) {
        log(DEBUG, "Should close connection");
    }

}
