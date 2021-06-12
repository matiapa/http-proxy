#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <address.h>
#include <logger.h>
#include <udp_utils.h>

#define ADDR_BUFF_SIZE 128


int create_udp_server(const char *port) {

	// Create address criteria

	struct addrinfo addrCriteria;
	memset(&addrCriteria, 0, sizeof(addrCriteria));

	addrCriteria.ai_family = AF_INET;
	addrCriteria.ai_flags = AI_PASSIVE;             // Accept on any address/port
	addrCriteria.ai_socktype = SOCK_DGRAM;
	addrCriteria.ai_protocol = IPPROTO_UDP;

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
		if (servSock < 0) {
			log(ERROR, "Creating passive socket");
			continue;
		}

    	setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));

		// Bind to address

		int bindRes = bind(servSock, addr->ai_addr, addr->ai_addrlen);
		if(bindRes < 0){
			log(ERROR, "Binding to server socket");
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

        char addressBuffer[ADDR_BUFF_SIZE];
        printSocketAddress((struct sockaddr *) &localAddr, addressBuffer);

        log(INFO, "Binding to %s", addressBuffer);
	}

	freeaddrinfo(servAddr);

	return servSock;

}

ssize_t uread(int fd, char * buffer, size_t buffSize, struct sockaddr * address, socklen_t * addressSize) {
	char addrBuffer[ADDR_BUFF_SIZE] = {0};

	ssize_t recvBytes = recvfrom(fd, buffer, buffSize, 0, address, addressSize);
    if (recvBytes < 0) {
      log(ERROR, "Recieving bytes: %s ", strerror(errno))
      return -1;
    }

	printSocketAddress((struct sockaddr *) address, addrBuffer);
    log(INFO, "Handling client %s - Received %zu bytes", addrBuffer, recvBytes);
	
	return recvBytes;
}

ssize_t usend(int fd, char * buffer, size_t buffSize, struct sockaddr * address, socklen_t addressSize) {
	char addrBuffer[ADDR_BUFF_SIZE] = {0};

	ssize_t sentBytes = sendto(fd, buffer, buffSize, 0, address, addressSize);
    if (sentBytes < 0){
      log(ERROR, "Sending bytes: %s", strerror(errno))
	  return -1;
	}

	printSocketAddress((struct sockaddr *) address, addrBuffer);
    log(DEBUG, "Handling client %s - Sent %zu bytes", addrBuffer, sentBytes);

	return sentBytes;
}
