#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <address.h>
#include <logger.h>
#include <selector.h>
#include <arpa/inet.h>
#include <tcp_utils.h>
#include <proxy.h>

#define MAX_PENDING_CONN 5
#define ADDR_BUFFER_SIZE 128


void print_address(int servSock) {
    struct sockaddr_storage localAddr;
    socklen_t addrSize = sizeof(localAddr);

    int getname = getsockname(servSock, (struct sockaddr *) &localAddr, &addrSize);
    if (getname < 0) {
        log(ERROR, "Getting master socket name");
    }

    char addressBuffer[ADDR_BUFFER_SIZE];
    sockaddr_print((struct sockaddr *) &localAddr, addressBuffer);

    log(INFO, "TCP: Binding to %s", addressBuffer);
}


int create_tcp_server(const char *ip, const char *port) {

    struct addrinfo addrCriteria;
    memset(&addrCriteria, 0, sizeof(addrCriteria));

	// Set address criteria

	addrCriteria.ai_family = AF_INET;
	addrCriteria.ai_flags = AI_PASSIVE;             // Accept on any address/port
	addrCriteria.ai_socktype = SOCK_STREAM;
	addrCriteria.ai_protocol = IPPROTO_TCP;

	int servSock = socket(addrCriteria.ai_family, addrCriteria.ai_socktype, addrCriteria.ai_protocol);
    if (servSock < 0){
        log(ERROR, "Creating passive socket");
        return -1;
    }
    log(DEBUG, "IPv4 socket %d created", servSock);

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    if (setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, (char *)&timeout, sizeof(int)) < 0) {
        log(ERROR, "set IPv4 socket options SO_REUSEADDR failed %s ", strerror(errno));
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(atoi(port));

    inet_pton(address.sin_family, ip, &address.sin_addr.s_addr);

    if (bind(servSock, (struct sockaddr *) &address, sizeof(address)) < 0) {
        log(ERROR, "bind for IPv4 failed");
        close(servSock);
    } else {
        if (listen(servSock, MAX_PENDING_CONN) < 0) {
            log(ERROR, "listen on IPv4 socket failes");
            close(servSock);
        } else {
            log(DEBUG, "Waiting for TCP IPv4 connections on socket %d\n", servSock);
        }
    }

    // Print local address
    print_address(servSock);

	return servSock;

}


int create_tcp6_server(const char *ip, const char *port) {

    struct addrinfo addrCriteria;
    memset(&addrCriteria, 0, sizeof(addrCriteria));

    // Set address criteria

    addrCriteria.ai_family = AF_INET6;
    addrCriteria.ai_flags = AI_PASSIVE;             // Accept on any address/port
    addrCriteria.ai_socktype = SOCK_STREAM;
    addrCriteria.ai_protocol = IPPROTO_TCP;

    int servSock = socket(addrCriteria.ai_family, addrCriteria.ai_socktype, addrCriteria.ai_protocol);
    if (servSock < 0){
        log(ERROR, "Creating passive socket for ipv6");
        return -1;
    }
    log(DEBUG, "IPv6 socket %d created", servSock);

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    if (setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, (char *)&timeout, sizeof(int)) < 0) {
        log(ERROR, "set IPv6 socket options SO_REUSEADDR failed for %s ", strerror(errno));
    }
    if (setsockopt(servSock, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&timeout, sizeof(int)) < 0) {
        log(ERROR, "set IPv6 socket options IPV6_V6ONLY failed %s ", strerror(errno));
    }

    struct sockaddr_in6 address;

    memset(&address, 0, sizeof(address));
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(atoi(port));

    inet_pton(address.sin6_family, ip, &address.sin6_addr);

    if (bind(servSock, (struct sockaddr *) &address, sizeof(address)) < 0) { //-V641
        log(ERROR, "bind for IPv6 failed");
        close(servSock);
    }
    else
    {
        if (listen(servSock, MAX_PENDING_CONN) < 0) {
            log(ERROR, "listen on IPv6 socket failes");
            close(servSock);
        } else {
            log(DEBUG, "Waiting for TCP IPv6 connections on socket %d\n", servSock);
        }
    }

    // Print local address
    print_address(servSock);

    return servSock;

}


int handle_passive_sockets(int sockets[], fd_handler ** handlers, int size) {

    // Initialize selector library

    // TODO: Move selector timeout
    const struct selector_init conf = {
        .signal = SIGCONT,
        .select_timeout = { .tv_sec  = 60, .tv_nsec = 0 }
    };

    if(selector_init(&conf) != 0) {
        log(ERROR, "Initializing selector library");
        return -1;
    }

    // Create new selector

    fdselector * selector_fd = selector_new(proxy_conf.maxClients);
    if(selector_fd == NULL) {
        log(ERROR, "Creating new selector");
        selector_close();
        return -1;
    }

    // Register passive sockets

    selector_status ss = SELECTOR_SUCCESS;

    for (int i = 0; i < size; i++) {
        if (sockets[i] != -1) {
            if (selector_fd_set_nio(sockets[i]) == -1){
                log(ERROR, "Setting master socket flags");
                return -1;
            }
            
            ss = selector_register(selector_fd, sockets[i], handlers[i], OP_READ, NULL);

            if (ss != SELECTOR_SUCCESS) {
                log(ERROR, "Registering master socket on selector");
                selector_destroy(selector_fd);
                selector_close();
                return -1;
            }
        }
    }

    // Start listening selector

    while(1) {
        ss = selector_select(selector_fd);

        if(ss != SELECTOR_SUCCESS) {
            log(ERROR, "Serving on selector");
            selector_destroy(selector_fd);
            selector_close();
            return -1;
        }
    }

}
