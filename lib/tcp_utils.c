#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include <address.h>
#include <logger.h>
#include <selector.h>
#include <doh_client.h>
#include <tcp_utils.h>

#define MAX_PENDING_CONN 5
#define ADDR_BUFFER_SIZE 128


int create_tcp_client(const char *host, const int port) {

	// Create address criteria

	char addrBuffer[ADDR_BUFFER_SIZE];
	struct addrinfo addrCriteria;
	memset(&addrCriteria, 0, sizeof(addrCriteria));

	addrCriteria.ai_family = AF_UNSPEC;
	addrCriteria.ai_socktype = SOCK_STREAM;
	addrCriteria.ai_protocol = IPPROTO_TCP;

    struct addrinfo * servAddr;

	// Resolve host string for posible addresses
	int getaddr = doh_client(host, port, &servAddr, AF_UNSPEC);

	if (getaddr != 0) {
		log(ERROR, "getaddrinfo() failed %s", gai_strerror(getaddr))
		return -1;
	}

	// Try to connect to an address

	int sock = -1;
	for (struct addrinfo * addr = servAddr; addr != NULL && sock == -1; addr = addr->ai_next) {
		sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		if (sock < 0){
			log(DEBUG, "Can't create client socket on %s", printAddressPort(addr, addrBuffer))
			continue;
		}

		int conn = connect(sock, addr->ai_addr, addr->ai_addrlen);
		if (conn != 0) {
			log(INFO, "can't connectto %s: %s", printAddressPort(addr, addrBuffer), strerror(errno))
			close(sock); // Socket connection failed; try next address
			sock = -1;
		}
	}

	// Release address resource and return socket number
    free(servAddr);
	return sock;

}


int create_tcp_server(const char *port) {

	// Create address criteria

	struct addrinfo addrCriteria;
	memset(&addrCriteria, 0, sizeof(addrCriteria));

	addrCriteria.ai_family = AF_INET;
	addrCriteria.ai_flags = AI_PASSIVE;             // Accept on any address/port
	addrCriteria.ai_socktype = SOCK_STREAM;
	addrCriteria.ai_protocol = IPPROTO_TCP;

	// Resolve service string for posible addresses

	struct addrinfo *servAddr;
	int getaddr = getaddrinfo(NULL, port, &addrCriteria, &servAddr);
	if (getaddr != 0) {
		log(ERROR, "getaddrinfo() failed %s", gai_strerror(getaddr));
        return -1;
	}

	// Try to bind to an address and to start listening on it

	int servSock = -1;
	for (struct addrinfo *addr = servAddr; addr != NULL && servSock == -1; addr = addr->ai_next) {

		// Create socket and make it reusable

		servSock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		if (servSock < 0){
			log(ERROR, "Creating passive socket");
			continue;
		}

    	setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));

		// Bind and listen

		int bindRes = bind(servSock, addr->ai_addr, addr->ai_addrlen);
		if(bindRes < 0){
			log(ERROR, "Binding to server socket");
			close(servSock);
			servSock = -1;
            continue;
		}

		int listenRes = listen(servSock, MAX_PENDING_CONN);
		if(listenRes < 0){
			log(ERROR, "Listening to server socket");
			close(servSock);
			servSock = -1;
            continue;
		}

		// Print local address

		struct sockaddr_storage localAddr;
		socklen_t addrSize = sizeof(localAddr);

		int getname = getsockname(servSock, (struct sockaddr *) &localAddr, &addrSize);
		if (getname < 0) {
            log(ERROR, "Getting master socket name");
            continue;
		}

        char addressBuffer[ADDR_BUFFER_SIZE];
        printSocketAddress((struct sockaddr *) &localAddr, addressBuffer);

        log(INFO, "Binding to %s", addressBuffer);
	}

	freeaddrinfo(servAddr);

	return servSock;

}


int handle_connections( int serverSocket, void (*handle_creates) (struct selector_key *key)) {

    if(selector_fd_set_nio(serverSocket) == -1) {
        log(ERROR, "Setting master socket flags");
        return -1;
    }

    // Initialize selector library

    const struct selector_init conf = {
        .signal = SIGALRM,
        .select_timeout = {
            .tv_sec  = 10,
            .tv_nsec = 0
        }
    };

    if(selector_init(&conf) != 0) {
        log(ERROR, "Initializing selector library");
        return -1;
    }

    // Create new selector

    fd_selector selector = selector_new(1024);
    if(selector == NULL) {
        log(ERROR, "Creating new selector");
        selector_close();
        return -1;
    }

    // Fill in handlers
    
    const struct fd_handler handlers = {
        .handle_create     = handle_creates,
        .handle_close      = NULL,  // TODO: Add a close handler
        .handle_block      = NULL
    };

    // Register master socket
    
    selector_status ss = SELECTOR_SUCCESS;

    ss = selector_register(selector, serverSocket, &handlers, OP_READ + OP_WRITE, NULL);

    if(ss != SELECTOR_SUCCESS) {
        log(ERROR, "Registering master socket on selector");
        selector_destroy(selector);
        selector_close();
        return -1;
    }

    // Start listening selector

    while(1) {
        ss = selector_select(selector);

        if(ss != SELECTOR_SUCCESS) {
            log(ERROR, "Serving on selector");
            selector_destroy(selector);
            selector_close();
            return -1;
        }
    }

}
