#include <logger.h>
#include <errno.h>
#include <string.h>
#include <buffer.h>
#include <stm.h>
#include <tcp_utils.h>
#include <http_request_parser.h>
#include <http.h>
#include <address.h>
#include <ctype.h>
#include <statistics.h>
#include <base64.h>
#include <dissector.h>
#include <doh_client.h>
#include <arpa/inet.h>
#include <proxy_stm.h>

#pragma GCC diagnostic ignored "-Wunused-parameter"


/* -------------------------------------- AUXILIARS PROTOTYPES -------------------------------------- */


/* ------------------------------------------------------------
  Processes an HTTP request and returns next state.
------------------------------------------------------------ */
static unsigned process_request(selector_key_t * key);

/* ------------------------------------------------------------
  Processes request headers according to RFC 7230 specs.
------------------------------------------------------------ */
static void process_request_headers(http_request * req, char * target_host, char * proxy_host);

/* ------------------------------------------------------------
  Processes request headers and deposits user:password into raw authorization.
------------------------------------------------------------ */
static int extract_http_credentials(http_request * request);


/* -------------------------------------- HANDLERS IMPLEMENTATIONS -------------------------------------- */


unsigned request_read_ready(unsigned int state, selector_key_t *key) {

    I(key)->last_activity = time(NULL);

    if (! buffer_can_write(&(I(key)->read_buffer))) {
        log_error("Read buffer limit reached");
        return notify_error(key, PAYLOAD_TOO_LARGE, REQUEST_READ);
    }

    // Read request bytes into read buffer

    size_t space;
    uint8_t * raw_req = buffer_write_ptr(&(I(key)->read_buffer), &space);
    ssize_t readBytes = read(I(key)->client_socket, raw_req, space);

    if(readBytes <= 0) {
        if(readBytes < 0 && errno != EBADF && errno != EPIPE)
            log_error("Failed to read from client");
        return CLIENT_CLOSE_CONNECTION;
    }

    buffer_write_adv(&(I(key)->read_buffer), readBytes);

    log(DEBUG, "Received %lu bytes from socket %d", (size_t) readBytes, I(key)->client_socket);

    // Calculate statistics

    add_bytes_recieved(readBytes);   

    // Process the request

    return process_request(key);

}


unsigned request_connect_write_ready(unsigned int state, selector_key_t *key) {

    int socket_error;
    socklen_t socket_error_len = sizeof(socket_error);
    int sock_opt = getsockopt(I(key)->target_socket, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len);
    if (sock_opt != 0) {
        log(ERROR, "Target getsockopt(%d) failed", I(key)->target_socket)
        perror("reason");
    }

    if (socket_error != 0) {
        close(I(key)->target_socket);
        if (I(key)->doh.server_socket > 0)
            return TRY_IPS;
        else
            return notify_error(key, BAD_GATEWAY, REQUEST_READ);
    } else { // la conexión funciono
        memcpy(&(I(key)->last_target_url), &(I(key)->doh.url), sizeof(struct url));
        if (I(key)->doh.server_socket > 0)
            doh_kill(key);
        // int aux = I(key)->doh.target_socket;
        // I(key)->target_socket = aux;
    }

    // Update last connection

    http_request * request = &(I(key)->req_parser.request);

    // Check for request method

    if (request->method == CONNECT) {

        // The request method is CONNECT, a response shall be sent
        // and TCP tunnel be established
        
        // Write response bytes into write buffer

        size_t space;
        char * ptr = (char *) buffer_write_ptr(&(I(key)->write_buffer), &space);

        http_response res = { .status = RESPONSE_OK };
        int written = write_response(&res, ptr, space, false);

        buffer_write_adv(&(I(key)->write_buffer), written);

        print_Access(inet_ntoa(I(key)->client.sin_addr), ntohs(I(key)->client.sin_port), I(key)->req_parser.request.url, I(key)->req_parser.request.method, 200);

        // Go to send response state

        return CONNECT_RESPONSE;

    } else { 

        // The request method is a traditional one, request shall be proccessed
        // and then forwarded
        
        if (request->method == OPTIONS && strlen(I(key)->last_target_url.path) == 0)
            sprintf(request->url, "*");

        // Process request headers

        char proxy_hostname[VIA_PROXY_NAME_SIZE] = {0};
        if(strlen(proxy_conf.viaProxyName) > 0) {
            strncpy(proxy_hostname, proxy_conf.viaProxyName, VIA_PROXY_NAME_SIZE);
        } else {
            get_machine_fqdn(proxy_hostname);
        }
        
        process_request_headers(request, I(key)->last_target_url.hostname, proxy_hostname);

        // Extract credentials if present

        if (proxy_conf.disectorsEnabled)
            extract_http_credentials(request);

        // Write processed request bytes into write buffer

        size_t space;
        char * ptr = (char *) buffer_write_ptr(&(I(key)->write_buffer), &space);

        int written = write_request(request, ptr, space, false);

        buffer_write_adv(&(I(key)->write_buffer), written);

        // Go to forward request state

        return REQUEST_FORWARD;

    }
}


unsigned request_forward_ready(unsigned int state, selector_key_t *key) {

    if (! buffer_can_read(&(I(key)->write_buffer)))
        return REQUEST_FORWARD;

    // Read request bytes from write buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(I(key)->write_buffer), &size);
    ssize_t sentBytes = write(I(key)->target_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write request to target");
        return TARGET_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(I(key)->write_buffer), sentBytes);

    log(DEBUG, "Sent %lu bytes to socket %d", (size_t) sentBytes, I(key)->target_socket);

    // Calculate statistics

    add_sent_bytes(sentBytes);

    if ((size_t) sentBytes < size)
        return REQUEST_FORWARD;
 
    if (I(key)->req_parser.request.message.body_length == 0) {
        return RESPONSE_READ;
    } else if (buffer_can_read(&(I(key)->read_buffer))) {
        // Body present, read some bytes
        size_t remaining;
        buffer_read_ptr(&(I(key)->read_buffer), &remaining);
        I(key)->req_parser.message_parser.current_body_length += remaining;
        return REQ_BODY_FORWARD;
    } else {
        // Body present, didn't read bytes
        return I(key)->req_parser.request.message.hasExpect
            ? TCP_TUNNEL : REQ_BODY_READ;
    }

}


unsigned req_body_read_ready(unsigned int state, selector_key_t *key) {

    I(key)->last_activity = time(NULL);

    if (! buffer_can_write(&(I(key)->read_buffer)))
        return REQ_BODY_FORWARD;

    // Read body bytes into read buffer

    size_t space;
    uint8_t * body = buffer_write_ptr(&(I(key)->read_buffer), &space);
    ssize_t readBytes = read(I(key)->client_socket, body, space);

    if(readBytes <= 0) {
        if(readBytes < 0 && errno != EBADF && errno != EPIPE)
            log_error("Failed to read from client");
        return CLIENT_CLOSE_CONNECTION;
    }

    buffer_write_adv(&(I(key)->read_buffer), readBytes);

    log(DEBUG, "Received %lu body bytes from socket %d", (size_t) readBytes, I(key)->client_socket);

    // Calculate statistics

    add_bytes_recieved(readBytes);

    http_request_parser * rp = &(I(key)->req_parser);

    if ((rp->message_parser.current_body_length += readBytes) < rp->request.message.body_length)
        return REQ_BODY_READ;
    else
        return REQ_BODY_FORWARD;

}


unsigned req_body_forward_ready(unsigned int state, selector_key_t *key) {

    // Will read the content directly from read buffer

    // Read body bytes from read buffer

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(I(key)->read_buffer), &size);
    ssize_t sentBytes = write(I(key)->target_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write body to target");
        return TARGET_CLOSE_CONNECTION;
    }

    buffer_read_adv(&(I(key)->read_buffer), sentBytes);

    log(DEBUG, "Sent %lu body bytes to socket %d", (size_t) sentBytes, I(key)->target_socket);

    // Calculate statistics

    add_sent_bytes(sentBytes);

    http_request_parser * rp = &(I(key)->req_parser);

    if ((size_t) sentBytes < size)
        return REQ_BODY_FORWARD;
    else if ((rp->message_parser.current_body_length) < rp->request.message.body_length)
        return REQ_BODY_READ;
    else
        return RESPONSE_READ;

}


unsigned connect_response_ready(unsigned int state, selector_key_t *key) {

    if (! buffer_can_read(&(I(key)->write_buffer)))
        return CONNECT_RESPONSE;

    // write response bytes to socket

    size_t size;
    uint8_t *ptr = buffer_read_ptr(&(I(key)->write_buffer), &size);
    ssize_t sentBytes = write(I(key)->client_socket, ptr, size);

    if (sentBytes < 0) {
        if(errno != EBADF && errno != EPIPE)
            log_error("Failed to write connect response to client");
        return END;
    }

    buffer_read_adv(&(I(key)->write_buffer), sentBytes);

    log(DEBUG, "Sent %lu bytes to socket %d", (size_t) sentBytes, I(key)->client_socket);

    //statistics
    add_sent_bytes(sentBytes);

    if ((size_t) sentBytes < size)
        return CONNECT_RESPONSE;

    memset(&(I(key)->pop3_parser), 0, sizeof(pop3_parser_data));
    pop3_parser_init(&(I(key)->pop3_parser));

    return TCP_TUNNEL;

}


/* -------------------------------------- AUXILIARS IMPLEMENTATIONS -------------------------------------- */


static unsigned process_request(selector_key_t * key) {

    // Parse the request and check for pending and failure cases

    parse_state parser_state = http_request_parser_parse(
        &(I(key)->req_parser), &(I(key)->read_buffer)
    );

    if (parser_state == PENDING)
        return REQUEST_READ;
        
    if (parser_state == FAILED)
        return notify_error(key, I(key)->req_parser.error_code, REQUEST_READ);

    http_request * request = &(I(key)->req_parser.request);

    // Parse the request target URL

    memset(&(I(key)->doh), 0, sizeof(struct doh_client));
    int r = parse_url(request->url, &(I(key)->doh.url));
    // TODO: Move parsed URL to another structure

    if (r < 0)
        return notify_error(key, BAD_REQUEST, REQUEST_READ);

    if (strlen(request->url) == 0)
        return notify_error(key, BAD_REQUEST, REQUEST_READ);

    if (request->method == TRACE)
        return notify_error(key, METHOD_NOT_ALLOWED, REQUEST_READ);

    // Log the access of the client

    log_client_access(I(key)->client_socket, request->url);

    // Prepare to connect to new target

    log(DEBUG, "Connection requested to %s:%d", I(key)->doh.url.hostname, I(key)->doh.url.port);

    // Check that target is not blacklisted

    if (strstr(proxy_conf.targetBlacklist, I(key)->doh.url.hostname) != NULL) {
        log(INFO, "Rejected connection to %s due to target blacklist", I(key)->doh.url.hostname);
        return notify_error(key, FORBIDDEN, REQUEST_READ);
    }

    // Prepare to resolve target address

    /*--------- Chequeo si el target esta en formato IP o es Localhost ---------*/
    struct addrinfo * addrinfo;
    if (resolve_string(&(addrinfo), I(key)->doh.url.hostname, I(key)->doh.url.port) >= 0) {

        if (is_proxy_host(addrinfo->ai_addr) && I(key)->doh.url.port == proxy_conf.proxyArgs.proxy_port) {
            log(INFO, "Prevented proxy loop")
            free(addrinfo);
            return notify_error(key, FORBIDDEN, REQUEST_READ);
        }

        int sock = socket(addrinfo->ai_family, addrinfo->ai_socktype, addrinfo->ai_protocol);

        selector_fd_set_nio(sock);

        if (sock < 0) 
            return notify_error(key, BAD_GATEWAY, REQUEST_READ);

        extern const struct fd_handler proxy_handlers;

        if(selector_register(key->s, sock, &proxy_handlers, OP_WRITE, key->data) != SELECTOR_SUCCESS) {
            log(ERROR, "Failed to register origin socket %d at selector", sock);
            return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);
        }
            
        if (connect(sock, addrinfo->ai_addr, addrinfo->ai_addrlen) == -1) {
            if (errno == EINPROGRESS) {
                I(key)->target_socket = sock;
                I(key)->references++;
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
