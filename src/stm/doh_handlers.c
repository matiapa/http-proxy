#include <logger.h>
#include <errno.h>
#include <string.h>
#include <stm.h>
#include <tcp_utils.h>
#include <http.h>
#include <address.h>
#include <doh_client.h>
#include <proxy_stm.h>

#define ADDR_BUFFER_SIZE 1024
#pragma GCC diagnostic ignored "-Wunused-parameter"


/* -------------------------------------- HANDLERS IMPLEMENTATIONS -------------------------------------- */


unsigned doh_connect_write_ready(unsigned int state, selector_key_t *key) {

    int socket_error;
    socklen_t socket_error_len = sizeof(socket_error);
    int sock_opt = getsockopt(I(key)->doh.server_socket, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len);
    if(sock_opt != 0){
        log(ERROR, "DoH server getsockopt(%d) failed", I(key)->doh.server_socket)
        perror("reason");
    }

    if(socket_error != 0){
        log(ERROR, "Failed to connect to DoH server")
        return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);
    }

    if (send_doh_request(key, I(key)->doh.family) < 0) {
        return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);
    }
    
    return RESPONSE_DOH;

}


unsigned response_doh_read_ready(unsigned int state, selector_key_t *key) {

    int ans_count = doh_client_read(key);
    if (ans_count == -1) {

        log(ERROR, "Failed to read response from DoH server")
        return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);

    } else if (ans_count == 0) {
        if (I(key)->doh.family == AF_INET) {
            free(I(key)->doh.target_address_list);
            I(key)->doh.family = AF_INET6;

            if (send_doh_request(key, I(key)->doh.family) < 0)
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


unsigned try_ips_arrival(const unsigned int state, selector_key_t *key) {
    char addrBuffer[ADDR_BUFFER_SIZE];

    struct addrinfo * current_addr = I(key)->doh.current_target_addr;
    struct addrinfo * address_list = I(key)->doh.target_address_list;

    // Try to connect to an address
    int target_socket = -1;
    while (current_addr != NULL && target_socket == -1) {
        if (is_proxy_host(current_addr->ai_addr) && I(key)->doh.url.port == proxy_conf.proxyArgs.proxy_port) {
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

    I(key)->doh.current_target_addr = current_addr;

    if (target_socket < 0) {
        if (I(key)->doh.family == AF_INET) {
            free(address_list);
            I(key)->doh.family = AF_INET6;
            I(key)->target_socket = I(key)->doh.server_socket; 
            // Esto último es provisional, por compatibilidad con los permisos de la STM

            if (send_doh_request(key, I(key)->doh.family) < 0)
                return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);

            return RESPONSE_DOH;
        } else {
            doh_kill(key);
            log(ERROR, "Connecting to target")
            return notify_error(key, INTERNAL_SERVER_ERROR, REQUEST_READ);
        }
    } else {
        I(key)->target_socket = target_socket;
        return REQUEST_CONNECT;
    }
}
