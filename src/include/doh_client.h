#ifndef PC_2021A_06_DOH_CLIENT_H
#define PC_2021A_06_DOH_CLIENT_H

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <address.h>
#include <sys/time.h>
#include <selector.h>
#include <proxy_args.h>

typedef struct doh_client {
    int                 family;
    struct addrinfo *   target_address_list;
    struct addrinfo *   current_target_addr;
    int                 server_socket;
    buffer              buff;
} doh_client;

void config_doh_client(struct doh * args);

int send_doh_request(selector_key_t *key, int type);

// Returns 0 on success, -1 on failure
int doh_client_init(selector_key_t *key);

// Returns the amount of answers obtained
int doh_client_read(selector_key_t *key);

int resolve_string(struct addrinfo ** addrinfo, const char * target, int port);

void doh_kill(selector_key_t * key);

#endif //PC_2021A_06_DOH_CLIENT_H
