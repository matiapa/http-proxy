#include <signal.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <statistics.h>
#include <tcp_utils.h>
#include <logger.h>
#include <args.h>
#include <monitor.h>
#include <doh_client.h>
#include <selector_enums.h>
#include <http_request_parser.h>
#include <http_response_parser.h>
#include <proxy_stm.h>

void handle_creates(struct selector_key *key);

void handle_creates(struct selector_key *key);

void handle_close(struct selector_key * key);

void sigpipe_handler(int signum);


void sigterm_handler(int signal);

pthread_t thread_monitor;

int main(int argc, char **argv) {

    // Read arguments and close stdin

    struct proxy_args args;
    parse_args(argc, argv, &args);

    proxy_conf.proxyArgs = args;

    close(0);

    // Register handlers for closing program appropiately

    signal(SIGTERM, sigterm_handler);
    signal(SIGINT,  sigterm_handler);
    signal(SIGPIPE, sigpipe_handler);

    // Initialize DOH client

    initialize_doh_client(&(args.doh));

    // Start monitor on another thread

    char mng_port[6] = {0};
    snprintf(mng_port, 6, "%d", args.mng_port);

    pthread_create(&thread_monitor, NULL, start_monitor, mng_port);

    // Initialize

    initialize_statistics();

    // Start accepting connections

    bool listen_ipv_both = false;
    if (args.proxy_addr == NULL) {
        args.proxy_addr = "0.0.0.0";
        listen_ipv_both = true;
    }

    struct addrinfo hint = { .ai_family = PF_UNSPEC, .ai_flags = AI_NUMERICHOST};
    struct addrinfo * listen_addr;
    if (getaddrinfo(args.proxy_addr, NULL, &hint, &listen_addr)) {
        log(ERROR, "Invalid proxy address");
        exit(EXIT_FAILURE);
    }

    char proxy_port[6] = {0};
    snprintf(proxy_port, 6, "%d", args.proxy_port);
      
    int sock_ipv4 = 0, sock_ipv6 = 0;

    if (listen_addr->ai_family == AF_INET || listen_ipv_both) {
        sock_ipv4 = create_tcp_server(args.proxy_addr, proxy_port);
        if(sock_ipv4 < 0) {
            log(ERROR, "Creating passive socket ipv4");
            exit(EXIT_FAILURE);
        }
    }

    if (listen_addr->ai_family == AF_INET6 || listen_ipv_both) {
        sock_ipv6 = create_tcp6_server(args.proxy_addr, proxy_port);
        if(sock_ipv6 < 0) {
            log(ERROR, "Creating passive socket ipv6");
            exit(EXIT_FAILURE);
        }
    }

    // Start handling connections

    int res = handle_connections(sock_ipv4, sock_ipv6, handle_creates);
    if(res < 0) {
        log(ERROR, "Handling connections");
        close(sock_ipv4);
        close(sock_ipv6);
        exit(EXIT_FAILURE);
    }

    close(sock_ipv4);
    close(sock_ipv6);

    return EXIT_SUCCESS;
    
}


void handle_creates(struct selector_key *key) {

    struct sockaddr_in address;
    int addrlen = sizeof(struct sockaddr_in);

    int masterSocket = key->item->master_socket;

    // Accept the client connection

    int clientSocket = accept(masterSocket, (struct sockaddr *) &address, (socklen_t *) &addrlen);
    if (clientSocket < 0) {
        log(FATAL, "Accepting new connection")
    }
    add_connection();

    key->item->client_socket = clientSocket;
    key->item->last_activity = time(NULL);

    log(INFO, "New connection - FD: %d - IP: %s - Port: %d\n", clientSocket, inet_ntoa(address.sin_addr),
        ntohs(address.sin_port));

    if (strstr(proxy_conf.clientBlacklist, inet_ntoa(address.sin_addr)) != NULL) {
        log(INFO, "Kicking %s due to blacklist", inet_ntoa(address.sin_addr));
        item_kill(key->s, key->item);
    }

    // Initialize connection buffers, state machine and HTTP parser

    buffer_init(&(key->item->read_buffer), CONN_BUFFER, malloc(CONN_BUFFER));
    buffer_init(&(key->item->write_buffer), CONN_BUFFER, malloc(CONN_BUFFER));

    memcpy(&(key->item->stm), &proto_stm, sizeof(proto_stm));
    stm_init(&(key->item->stm));

    http_request_parser_init(&(key->item->req_parser));
    http_response_parser_init(&(key->item->res_parser));
    pop3_parser_init(&(key->item->pop3_parser));

    // Set initial interests

    key->item->client_interest = OP_READ;
    key->item->target_interest = OP_NOOP;
    selector_update_fdset(key->s, key->item);

}

void sigterm_handler(int signal) {
    printf("signal %d, cleaning up selector and exiting\n",signal);
    selector_destroy(selector_fd); // destruyo al selector
    pthread_kill(thread_monitor, SIGINT); // destruyo al monitor
    _exit(EXIT_SUCCESS);
}

void sigpipe_handler(int signum) {
    printf("Caught signal SIGPIPE %d\n",signum);
}

