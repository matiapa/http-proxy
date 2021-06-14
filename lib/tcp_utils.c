#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include <address.h>
#include <logger.h>
#include <selector.h>
#include <doh_client.h>
#include <arpa/inet.h>
#include <tcp_utils.h>

#define MAX_PENDING_CONN 5
#define ADDR_BUFFER_SIZE 128

void handle_close(struct selector_key * key);

fd_selector selector_fd;


int create_tcp_client(const char *host, const int port) {

	// Create address criteria

	char addrBuffer[ADDR_BUFFER_SIZE];
	struct addrinfo addrCriteria;
	memset(&addrCriteria, 0, sizeof(addrCriteria));

	addrCriteria.ai_family = AF_UNSPEC;
	addrCriteria.ai_socktype = SOCK_STREAM;
	addrCriteria.ai_protocol = IPPROTO_TCP;

    struct addrinfo * servAddr=NULL;

    int types[2] = {AF_INET, AF_INET6};

    int sock = -1;
    for (int i = 0; i < 2 && sock == -1; i++) {

        // Resolve host string for posible addresses
        int getaddr = doh_client(host, port, &servAddr, types[i]);

        if (getaddr != 0) {
            log(ERROR, "doh_client() failed %s", gai_strerror(getaddr))
            return -1;
        }

        // Try to connect to an address

        sock = -1;
        for (struct addrinfo * addr = servAddr; addr != NULL && sock == -1; addr = addr->ai_next) {
            sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
            if (sock < 0){
                sockaddr_print(addr->ai_addr, addrBuffer);
                log(DEBUG, "Can't create client socket on %s", addrBuffer)
                continue;
            }

            int conn = connect(sock, addr->ai_addr, addr->ai_addrlen);
            if (conn != 0) {
                sockaddr_print(addr->ai_addr, addrBuffer);
                log(INFO, "can't connect to %s: %s", addrBuffer, strerror(errno))
                close(sock); // Socket connection failed; try next address
                sock = -1;
            }
        }

        // Release address resource and return socket number
        if(servAddr != NULL){
            free(servAddr);
        }
        
    }

    if (sock < 0) {
        log(ERROR, "Connecting to target")
    } else {
        log(INFO, "Connected to target (DoH)")
    }

	return sock;

}


void print_address(int servSock) {
    struct sockaddr_storage localAddr;
    socklen_t addrSize = sizeof(localAddr);

    int getname = getsockname(servSock, (struct sockaddr *) &localAddr, &addrSize);
    if (getname < 0) {
        log(ERROR, "Getting master socket name");
    }

    char addressBuffer[ADDR_BUFFER_SIZE];
    sockaddr_print((struct sockaddr *) &localAddr, addressBuffer);

    log(INFO, "Binding to %s", addressBuffer);
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

int handle_connections(int sock_ipv4, int sock_ipv6, void (*handle_creates) (struct selector_key *key)) {

    // Initialize selector library

    const struct selector_init conf = {
        .signal = SIGCONT,
        .select_timeout = {
            .tv_sec  = 10,
            .tv_nsec = 0
        }
    };

    if(selector_init(&conf) != 0) {
        log(ERROR, "Initializing selector library");
        return -1;
    }

    // Fill in handlers

    const struct fd_handler handlers = {
        .handle_create = handle_creates,
        .handle_close = handle_close,
        .handle_block = NULL};

    // Create new selector

    selector_fd = selector_new(proxy_conf.maxClients, &handlers);
    if(selector_fd == NULL) {
        log(ERROR, "Creating new selector");
        selector_close();
        return -1;
    }


    // Register master socket

    selector_status ss = SELECTOR_SUCCESS;

    int master_sockets[MASTER_SOCKET_SIZE] = {sock_ipv4, sock_ipv6};
    for (int i = 0; i < MASTER_SOCKET_SIZE; i++) {
        if (master_sockets[i] != -1) {
            if (selector_fd_set_nio(master_sockets[i]) == -1){
                log(ERROR, "Setting master socket flags");
                return -1;
            }
            
            ss = selector_register(selector_fd, master_sockets[i], OP_READ + OP_WRITE, NULL);

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

void handle_close(struct selector_key * key) {
    log(INFO, "Destroying item with client socket %d and target socket %d", key->item->client_socket, key->item->target_socket);
    item_kill(key->s, key->item);
}
