#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <buffer.h>
#include <stm.h>
#include <http_request_parser.h>
#include <http_response_parser.h>
#include <pop3_parser.h>
#include <doh_client.h>
#include <proxy_stm.h>
#include <logger.h>
#include <statistics.h>
#include <proxy.h>

#define CONN_BUFFER (1024 * 1024 * 5)

#define N(x) (sizeof(x)/sizeof((x)[0]))


proxy_item * items;


static struct proxy_item * proxy_item_new(int client_fd) {
    // Allocate new item

    struct proxy_item * new_item = calloc(1, sizeof(*new_item));
    if(new_item == NULL)
        goto finally;

    // Set client/origin fds
    
    new_item->client_socket = client_fd;
    new_item->target_socket = -1;
    new_item->references = 1;

    // Allocate item data

    buffer_init(&(new_item->read_buffer), CONN_BUFFER, malloc(CONN_BUFFER));
    buffer_init(&(new_item->write_buffer), CONN_BUFFER, malloc(CONN_BUFFER));

    memcpy(&(new_item->stm), &proto_stm, sizeof(proto_stm));
    stm_init(&(new_item->stm));

    http_request_parser_init(&(new_item->req_parser));
    http_response_parser_init(&(new_item->res_parser));
    pop3_parser_init(&(new_item->pop3_parser));

    // Link item into list

    struct proxy_item * last_item = items;
    if (last_item == NULL)
        items = new_item;
    else {
        while (last_item->next != NULL)
            last_item = last_item->next;
        last_item->next = new_item;
    }

    // Return it
        
finally:
    return new_item;
}

static void proxy_item_destroy(selector_key_t* key) {
    proxy_item * item = I(key);

    // Free item data

    free(item->read_buffer.data);
    free(item->write_buffer.data);

    http_request_parser_destroy(&(item->req_parser));
    http_response_parser_destroy(&(item->res_parser));
    pop3_parser_destroy(&(item->pop3_parser));

    // Unlink the item

    struct proxy_item * prev_item = items;
    if (prev_item == item)
        items = NULL;
    else {
        while (prev_item->next != item && prev_item->next != NULL)
            prev_item = prev_item->next;
        prev_item->next = item->next;
    }

    // Free it

    free(item);
}


static void proxy_handle_read(selector_key_t *key) {
    struct state_machine *stm = &I(key)->stm;
    stm_handler_read(stm, key);
}


static void proxy_handle_write(selector_key_t *key) {
    struct state_machine *stm = &I(key)->stm;
    stm_handler_write(stm, key);
}


static void proxy_handle_block(selector_key_t *key) {
    struct state_machine *stm = &I(key)->stm;
    stm_handler_block(stm, key);
}


static void proxy_handle_close(selector_key_t *key) {
    // Item will be destroyed only after every fd has been unregistered
    if(--(I(key)->references) == 0)
        proxy_item_destroy(key);
}


const struct fd_handler proxy_handlers = {
    .handle_read   = proxy_handle_read,
    .handle_write  = proxy_handle_write,
    .handle_close  = proxy_handle_close,
    .handle_block  = proxy_handle_block,
};


void proxy_passive_accept(selector_key_t * key) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Accept the connection and set O_NONBLOCK

    const int client = accept(key->fd, (struct sockaddr*) &client_addr, &client_addr_len);
    if(client == -1)
        goto fail;
    
    if(selector_fd_set_nio(client) == -1)
        goto fail;

    // Check that client is not blacklisted

    if (strstr(proxy_conf.clientBlacklist, inet_ntoa(client_addr.sin_addr)) != NULL) {
        log(INFO, "Rejecting %s due to client blacklist", inet_ntoa(client_addr.sin_addr));
        goto fail;
    }

    log(INFO, "Accepted client %s:%d (FD: %d)", inet_ntoa(client_addr.sin_addr),
        ntohs(client_addr.sin_port), key->fd);

    // Create a new item
    
    struct proxy_item * item = proxy_item_new(client);
    if(item == NULL)
        goto fail;
    
    memcpy(&item->client_addr, &client_addr, client_addr_len);

    item->last_activity = time(NULL);

    if(selector_register(key->s, client, &proxy_handlers, OP_READ, item) != SELECTOR_SUCCESS)
        goto fail;

    // Register new connection

    add_connection();
    
    return;

fail:
    if(client != -1)
        close(client);
    proxy_item_destroy(key);
}


void proxy_item_finish(selector_key_t * key) {
    proxy_item * item = I(key);

    // Just unregister file descriptors, this will trigger proxy_handle_close
    // where everything else is closed appropiately

    const int fds[] = { item->client_socket, item->target_socket, item->doh.server_socket };
    for(unsigned i = 0; i < N(fds); i++) {
        if(fds[i] != -1) {
            if(selector_unregister_fd(key->s, fds[i]) != SELECTOR_SUCCESS)
                abort();
            close(fds[i]);
        }
    }
}


void proxy_item_reset(selector_key_t * key) {
    proxy_item * item = I(key);

    // Reset parsers and unregister and close only target socket

    http_request_parser_reset(&(item->req_parser));
    http_response_parser_reset(&(item->res_parser));
    pop3_parser_reset(&(item->pop3_parser));

    selector_unregister_fd(key->s, item->target_socket);
    close(item->target_socket);
}