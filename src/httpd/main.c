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
#include <udp_utils.h>

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

    log(INFO, "Welcome to HTTP Proxy!");

    // Register handlers for closing program appropiately

    signal(SIGTERM, sigterm_handler);
    signal(SIGINT,  sigterm_handler);
    signal(SIGPIPE, sigpipe_handler);

    // Initialize DOH client

    config_doh_client(&(args.doh));

    // Initialize

    initialize_statistics();

    // Start accepting connections

    int master_sockets[MASTER_SOCKET_SIZE] = {-1, -1};
    int i = 0;
    char *ipv4_addr, *ipv6_addr;
    if (args.proxy_addr == NULL) {
        ipv4_addr = "0.0.0.0";
        ipv6_addr = "::0";
    } else {
        struct addrinfo hint = { .ai_family = PF_UNSPEC, .ai_flags = AI_NUMERICHOST};
        struct addrinfo * listen_addr;
        if (getaddrinfo(args.proxy_addr, NULL, &hint, &listen_addr)) {
            log(ERROR, "Invalid proxy address");
            exit(EXIT_FAILURE);
        }
        ipv4_addr = listen_addr->ai_family == AF_INET ? args.proxy_addr : NULL;
        ipv6_addr = listen_addr->ai_family == AF_INET6 ? args.proxy_addr : NULL;
    }

    char proxy_port[6] = {0};
    snprintf(proxy_port, 6, "%d", args.proxy_port);

    if (ipv4_addr != NULL) {
        master_sockets[i] = create_tcp_server(ipv4_addr, proxy_port);
        if(master_sockets[i] < 0) {
            log(ERROR, "Creating passive socket ipv4");
            exit(EXIT_FAILURE);
        }
        i++;
    }

    if (ipv6_addr != NULL) {
        master_sockets[i] = create_tcp6_server(ipv6_addr, proxy_port);
        if(master_sockets[i] < 0) {
            log(ERROR, "Creating passive socket ipv6");
            exit(EXIT_FAILURE);
        }
    }

    // Start monitor

    int udp_sockets[MASTER_SOCKET_SIZE] = {-1, -1};
    i = 0;
    ipv4_addr = NULL;
    ipv6_addr = NULL;
    if (args.mng_addr == NULL) {
        ipv4_addr = "127.0.0.1";
        ipv6_addr = "::1";
    } else {
        struct addrinfo hint = { .ai_family = PF_UNSPEC, .ai_flags = AI_NUMERICHOST};
        struct addrinfo * listen_addr;
        if (getaddrinfo(args.mng_addr, NULL, &hint, &listen_addr)) {
            log(ERROR, "Invalid proxy address");
            exit(EXIT_FAILURE);
        }
        ipv4_addr = listen_addr->ai_family == AF_INET ? args.proxy_addr : NULL;
        ipv6_addr = listen_addr->ai_family == AF_INET6 ? args.proxy_addr : NULL;
    }

    if (ipv4_addr != NULL) {
        udp_sockets[i] = create_udp_server(ipv4_addr, proxy_conf.proxyArgs.mng_port);
        if (udp_sockets[i] < 0) {
            log(ERROR, "Creating passive socket ipv4");
            exit(EXIT_FAILURE);
        }
        i++;
    }

    if (ipv6_addr != NULL) {
        udp_sockets[i] = create_udp6_server(ipv6_addr, proxy_conf.proxyArgs.mng_port);
        if (udp_sockets[i] < 0) {
            log(ERROR, "Creating passive socket ipv6");
            exit(EXIT_FAILURE);
        }
    }

    // Start handling connections
    int res = handle_connections(master_sockets, udp_sockets, handle_creates);
    if(res < 0) {
        log(ERROR, "Handling connections");
        close(master_sockets[0]);
        close(master_sockets[1]);
        close(udp_sockets[0]);
        close(udp_sockets[1]);
        exit(EXIT_FAILURE);
    }

    close(master_sockets[0]);
    close(master_sockets[1]);
    close(udp_sockets[0]);
    close(udp_sockets[1]);

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
    key->item->client=address;

    log(INFO, "Accepted client %s:%d (FD: %d)", inet_ntoa(address.sin_addr),
        ntohs(address.sin_port), clientSocket);

    if (strstr(proxy_conf.clientBlacklist, inet_ntoa(address.sin_addr)) != NULL) {
        log(INFO, "Rejecting %s due to client blacklist", inet_ntoa(address.sin_addr));
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

