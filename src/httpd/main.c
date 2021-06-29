#include <signal.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <logger.h>
#include <proxy_args.h>
#include <statistics.h>
#include <doh_client.h>
#include <tcp_utils.h>
#include <udp_utils.h>
#include <proxy_stm.h>
#include <monitor.h>


void sigpipe_handler(int signum);

void sigterm_handler(int signal);


int main(int argc, char **argv) {

    close(0);

    // Read arguments

    struct proxy_args args;
    parse_args(argc, argv, &args);
    proxy_conf.proxyArgs = args;

    log(INFO, "Welcome to HTTP Proxy!");

    // Register process signal handlers

    signal(SIGTERM, sigterm_handler);
    signal(SIGINT,  sigterm_handler);
    signal(SIGPIPE, sigpipe_handler);

    // Initialize DOH client

    config_doh_client(&(args.doh));

    // Initialize statistics

    initialize_statistics();

    // Create passive sockets

    int passive_sockets[4] = {-1};
    fd_handler * handlers[4];
    int size = 0;

    // Check for proxy IPV4/IPV6 settings

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

    // Create proxy IPV4/IPV6 sockets

    fd_handler proxy_handler = { .handle_read = proxy_passive_accept };

    if (ipv4_addr != NULL) {
        passive_sockets[size] = create_tcp_server(ipv4_addr, proxy_port);
        if(passive_sockets[size] < 0)
            goto error;
        handlers[size] = &proxy_handler;
        size++;
    }

    if (ipv6_addr != NULL) {
        passive_sockets[size] = create_tcp6_server(ipv6_addr, proxy_port);
        if(passive_sockets[size] < 0)
            goto error;
        handlers[size] = &proxy_handler;
        size++;
    }

    // Check for monitor IPV4/IPV6 settings

    ipv4_addr = NULL; ipv6_addr = NULL;
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

    // Create monitor IPV4/IPV6 sockets

    fd_handler monitor_handler = { .handle_read = handle_read_monitor };

    if (ipv4_addr != NULL) {
        passive_sockets[size] = create_udp_server(ipv4_addr, proxy_conf.proxyArgs.mng_port);
        if(passive_sockets[size] < 0)
            goto error;
        handlers[size] = &monitor_handler;
        size++;
    }

    if (ipv6_addr != NULL) {
        passive_sockets[size] = create_udp6_server(ipv6_addr, proxy_conf.proxyArgs.mng_port);
        if(passive_sockets[size] < 0)
            goto error;
        handlers[size] = &monitor_handler;
        size++;
    }

    // Start handling passive sockets

    handle_passive_sockets(passive_sockets, handlers, size);

    for(int i=0; i<size; i++)
        if (passive_sockets[i] != -1)
            close(passive_sockets[i]);

    return EXIT_SUCCESS;

error:
    for(int i=0; i<size; i++)
        if (passive_sockets[i] != -1)
            close(passive_sockets[i]);

    return EXIT_FAILURE;

}


void sigterm_handler(int signal) {
    // TODO: Handle properly
    printf("signal %d, cleaning up selector and exiting\n",signal);
    _exit(EXIT_SUCCESS);
}


void sigpipe_handler(int signum) {
    printf("Caught signal SIGPIPE %d\n",signum);
}
