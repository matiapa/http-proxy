#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "../include/logger.h"
#include "../include/address.h"
#include "../include/server.h"

#define MAX_PENDING_CONN 5
#define MAX_ADDR_BUFFER 128
#define MAX_CLIENTS 500

static char addrBuffer[MAX_ADDR_BUFFER];

/* --------------------------------------------------
   Resolves address and creates passive socket
-------------------------------------------------- */

int setupServerSocket(const char *service) {

	// Create address criteria

	struct addrinfo addrCriteria;
	memset(&addrCriteria, 0, sizeof(addrCriteria));

	addrCriteria.ai_family = AF_UNSPEC;
	addrCriteria.ai_flags = AI_PASSIVE;             // Accept on any address/port
	addrCriteria.ai_socktype = SOCK_STREAM;
	addrCriteria.ai_protocol = IPPROTO_TCP;

	// Resolve service string for posible addresses

	struct addrinfo *servAddr;
	int getaddr = getaddrinfo(NULL, service, &addrCriteria, &servAddr);
	if (getaddr != 0) {
		log(FATAL, "getaddrinfo() failed %s", gai_strerror(getaddr));
		return -1;
	}

	// Try to bind to an address and to start listening on it

	int servSock = -1;
	for (struct addrinfo *addr = servAddr; addr != NULL && servSock == -1; addr = addr->ai_next) {

		// Create socket and make it reusable

		servSock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		if (servSock < 0){
			log(FATAL, "Creating passive socket");
			continue;
		}

		int opt = 1;
    	setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

		// Bind and listen

		int bindRes = bind(servSock, addr->ai_addr, addr->ai_addrlen);
		if(bindRes < 0){
			log(FATAL, "Binding to server socket");
			close(servSock);
			servSock = -1;
		}

		int listenRes = listen(servSock, MAX_PENDING_CONN);
		if(listenRes < 0){
			log(FATAL, "Listening to server socket");
			close(servSock);
			servSock = -1;
		}

		// Print local address

		struct sockaddr_storage localAddr;
		socklen_t addrSize = sizeof(localAddr);

		int getname = getsockname(servSock, (struct sockaddr *) &localAddr, &addrSize);
		if (getname >= 0) {
			printSocketAddress((struct sockaddr *) &localAddr, addrBuffer);
			log(INFO, "Binding to %s", addrBuffer);
		}
	}

	freeaddrinfo(servAddr);

	return servSock;

}


/* --------------------------------------------------
   Recieves passive socket and creates active socket
   when a new connection is available
-------------------------------------------------- */

void handleClients(int passiveSocket, void (*conn_handler)(int), int (*io_handler)(int)) {

    // Accept the incoming connection

    int client_sockets[MAX_CLIENTS] = {0};
    fd_set readfds;

    struct sockaddr_in address;
    int addrlen = sizeof(address);
     
    while(1) {
        // Clean socket set an add master socket fd

        FD_ZERO(&readfds);
        FD_SET(passiveSocket, &readfds);
         
        // Add child sockets fds and get max_sd

        int max_sd = passiveSocket;
        for (int i = 0 ; i < MAX_CLIENTS ; i++) {
            int sd = client_sockets[i];

            if(sd > 0)
                FD_SET( sd , &readfds);
             
            if(sd > max_sd)
                max_sd = sd;
        }
  
        // Wait for an activity on one of the sockets, timeout is NULL so wait indefinitely

        int activity = select(max_sd + 1 , &readfds , NULL , NULL , NULL);
        if ((activity < 0) && (errno != EINTR)) {
            log(FATAL, "On select")
        }
          
        // If something happened on the master socket, then its an incoming connection

        if (FD_ISSET(passiveSocket, &readfds)) {

            // Accept the connection

            int new_socket = accept(passiveSocket, (struct sockaddr *) &address, (socklen_t*) &addrlen);
            if (new_socket < 0){
                log(FATAL, "Accepting new connection")
                exit(EXIT_FAILURE);
            }

            printf("New connection - FD: %d - IP: %s - Port: %d\n", new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

            // Add new socket to array of sockets

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if(client_sockets[i] == 0 ) {
                    client_sockets[i] = new_socket;
                    printf("Adding to list of sockets as %d\n" , i);
                    break;
                }
            }
                  
            conn_handler(new_socket);
                            
        }
          
        // Otherwise is some IO operation on a child socket

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
              
            if (FD_ISSET(sd , &readfds)) {

                int readBytes = io_handler(sd);

                if (readBytes == 0) {
                    // Client disconnected, get his details and print
                    getpeername(sd, (struct sockaddr*) &address, (socklen_t*) &addrlen);
                    printf("Closed connection - IP: %s - Port: %d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
                      
                    // Close the socket and mark as 0 in list for reuse
                    close(sd);
                    client_sockets[i] = 0;
                    continue;
                }
            }
        }
    }

}