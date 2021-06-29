#ifndef PROXY_STM_H
#define PROXY_STM_H

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

/* -------------------------------------- REQUEST HANDLERS -------------------------------------- */

/* ------------------------------------------------------------
  Reads HTTP requests message part from client.
------------------------------------------------------------ */
unsigned request_read_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Handles the completion of connection to target.
------------------------------------------------------------ */
unsigned request_connect_write_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP requests message part to target.
------------------------------------------------------------ */
unsigned request_forward_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Reads HTTP requests body part from client.
------------------------------------------------------------ */
unsigned req_body_read_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP requests body part to target.
------------------------------------------------------------ */
unsigned req_body_forward_ready(unsigned int state, struct selector_key *key);

/* -------------------------------------- DOH HANDLERS -------------------------------------- */

/* ------------------------------------------------------------
  Sends connection response to client.
------------------------------------------------------------ */
unsigned connect_response_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Handles the completion of connection to DoH.
------------------------------------------------------------ */
unsigned doh_connect_write_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Reads DoH response from server.
------------------------------------------------------------ */
unsigned response_doh_read_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Tries to connect to a target resolved IP.
------------------------------------------------------------ */
unsigned try_ips_arrival(const unsigned int state, struct selector_key *key);

/* -------------------------------------- RESPONSE HANDLERS -------------------------------------- */

/* ------------------------------------------------------------
  Reads HTTP responses message part from target.
------------------------------------------------------------ */
unsigned response_read_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP responses message part to client.
------------------------------------------------------------ */
unsigned response_forward_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Reads HTTP responses body part from target.
------------------------------------------------------------ */
unsigned res_body_read_ready(unsigned int state, struct selector_key *key);

/* ------------------------------------------------------------
  Forwards HTTP responses body part to client.
------------------------------------------------------------ */
unsigned res_body_forward_ready(unsigned int state, struct selector_key *key);

/* -------------------------------------- AUXILIARS PROTOTYPES -------------------------------------- */

#define log_error(_description) \
    log(ERROR, "At state %u: %s", key->item->stm.current->state, _description);

#define remove_array_elem(array, pos, size) \
  memcpy(array+pos, array+pos+1, size-pos-1)

#define rtrim(s) \
    char* back = s + strlen(s); \
    while(isspace(*--back)); \
    *(back+1) = 0;

unsigned notify_error(struct selector_key *key, int status_code, unsigned next_state);

#endif