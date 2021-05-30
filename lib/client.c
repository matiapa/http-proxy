#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "../include/address.h"
#include "../include/logger.h"
#include "../include/client.h"

#define MAX_ADDR_BUFFER 128

int tcpClientSocket(const char *host, const char *service) {

	// Create address criteria

	char addrBuffer[MAX_ADDR_BUFFER];
	struct addrinfo addrCriteria;
	memset(&addrCriteria, 0, sizeof(addrCriteria));

	addrCriteria.ai_family = AF_UNSPEC;
	addrCriteria.ai_socktype = SOCK_STREAM;
	addrCriteria.ai_protocol = IPPROTO_TCP;

	// Resolve host string for posible addresses

	struct addrinfo *servAddr;
	int getaddr = getaddrinfo(host, service, &addrCriteria, &servAddr);
	if (getaddr != 0) {
		log(ERROR, "getaddrinfo() failed %s", gai_strerror(getaddr))
		return -1;
	}

	// Try to connect to an address

	int sock = -1;
	for (struct addrinfo *addr = servAddr; addr != NULL && sock == -1; addr = addr->ai_next) {
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

	freeaddrinfo(servAddr); 

	return sock;

}
