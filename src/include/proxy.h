#ifndef PROXY_H
#define PROXY_H

#include <netdb.h>
#include <time.h>

#include <selector.h>
#include <buffer.h>
#include <stm.h>

#include <http_request_parser.h>
#include <http_response_parser.h>
#include <pop3_parser.h>
#include <doh_client.h>

typedef struct proxy_item {
    int                 client_socket;
    struct sockaddr_in  client_addr;

    int                 target_socket;
    url_t               target_url;

    buffer              read_buffer;
    buffer              write_buffer;
    
    state_machine       stm;
    doh_client          doh;

    http_request_parser  req_parser;
    http_response_parser res_parser;
    pop3_parser_data     pop3_parser;

    time_t              last_activity;
    
    int                 references;
    void                * data;
    struct proxy_item   * next;
} proxy_item;

void proxy_passive_accept(selector_key_t *key);

void proxy_item_reset(selector_key_t * key);

void proxy_item_finish(selector_key_t * key);

#define I(key) ((proxy_item *) key->data)

#endif
