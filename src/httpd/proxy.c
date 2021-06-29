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


proxy_item * pool;
static const unsigned  max_pool  = 50;
static unsigned        pool_size = 0;



static struct proxy_item * proxy_item_new(int client_fd) {
    struct proxy_item * ret;

    if(pool == NULL) {
        ret = calloc(1, sizeof(*ret));
    } else {
        ret       = pool;
        pool      = pool->next;
        ret->next = 0;
    }
    if(ret == NULL)
        goto finally;
    
    ret->target_socket       = -1;
    ret->client_socket       = client_fd;

    buffer_init(&(ret->read_buffer), CONN_BUFFER, malloc(CONN_BUFFER));
    buffer_init(&(ret->write_buffer), CONN_BUFFER, malloc(CONN_BUFFER));

    memcpy(&(ret->stm), &proto_stm, sizeof(proto_stm));
    stm_init(&(ret->stm));

    http_request_parser_init(&(ret->req_parser));
    http_response_parser_init(&(ret->res_parser));
    pop3_parser_init(&(ret->pop3_parser));

    ret->references = 1;
finally:
    return ret;
}


static void proxy_item_destroy(struct proxy_item *s) {
    if(s != NULL && s->references == 1) {
       if(pool_size < max_pool) {
            s->next = pool;
            pool    = s;
            pool_size++;
        } else {
            // TODO: Handle this correctly
            free(s);
        }
    } else if (s != NULL) {
        s->references -= 1;
    }
}


static void proxy_pool_destroy(void) {
    struct proxy_item *next, *s;
    for(s = pool; s != NULL ; s = next) {
        next = s->next;
        free(s);
    }
}


static void proxy_item_done(selector_key_t* key) {
    const int fds[] = { I(key)->client_socket, I(key)->target_socket };
    for(unsigned i = 0; i < N(fds); i++) {
        if(fds[i] != -1) {
            if(selector_unregister_fd(key->s, fds[i]) != SELECTOR_SUCCESS)
                abort();
            close(fds[i]);
        }
    }
}


static void proxy_handle_read(selector_key_t *key) {
    struct state_machine *stm = &I(key)->stm;
    const enum proxy_state st = stm_handler_read(stm, key);

    if(st == ERROR_STATE)
        proxy_item_done(key);
}


static void proxy_handle_write(selector_key_t *key) {
    struct state_machine *stm = &I(key)->stm;
    const enum proxy_state st = stm_handler_write(stm, key);

    if(st == ERROR_STATE)
        proxy_item_done(key);
}


static void proxy_handle_block(selector_key_t *key) {
    struct state_machine *stm = &I(key)->stm;
    const enum proxy_state st = stm_handler_block(stm, key);

    if(st == ERROR_STATE)
        proxy_item_done(key);
}


static void proxy_handle_close(selector_key_t *key) {
    proxy_item_done(key);
}


void proxy_passive_accept(selector_key_t *key) {
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
    
    memcpy(&item->client, &client_addr, client_addr_len);

    item->last_activity = time(NULL);

    const struct fd_handler proxy_handlers = {
        .handle_read   = proxy_handle_read,
        .handle_write  = proxy_handle_write,
        .handle_close  = proxy_handle_close,
        .handle_block  = proxy_handle_block,
    };

    if(selector_register(key->s, client, &proxy_handlers, OP_READ, item) != SELECTOR_SUCCESS)
        goto fail;

    // Register new connection

    add_connection();
    
    return;

fail:
    if(client != -1)
        close(client);
    proxy_item_destroy(item);
}
