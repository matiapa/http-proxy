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
#include "../include/client.h"
#include "../include/io.h"

#define MAX_PENDING_CONN 5
#define MAX_ADDR_BUFFER 128

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

		int opt = 1;
    	setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

		// Bind and listen

		int bindRes = bind(servSock, addr->ai_addr, addr->ai_addrlen);
		if(bindRes < 0){
			log(ERROR, "Binding to server socket");
			close(servSock);
			servSock = -1;
		}

		int listenRes = listen(servSock, MAX_PENDING_CONN);
		if(listenRes < 0){
			log(ERROR, "Listening to server socket");
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

void handleConnections(int passiveSocket, int (*read_handler)(int), int (*write_handler)(int)) {

    // Accept the incoming connection

    struct sockaddr_in address;
    int addrlen = sizeof(address);

    fd_set readfds;
    fd_set writefds;

    FD_ZERO(&writefds);
     
    while(1) {
        // Clean read socket set an add passive socket fd

        FD_ZERO(&readfds);
        FD_SET(passiveSocket, &readfds);
         
        // Add child sockets fds and get max_sd

        int max_socket = passiveSocket;
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            connection conn = connections[i];
            int socket = conn.src_socket;

            if(socket == 0)
                continue;

            FD_SET(socket , &readfds);
             
            if(socket > max_socket)
                max_socket = socket;
        }
  
        // Wait for an activity on one of the sockets, timeout is NULL so wait indefinitely

        int activity = select(max_socket + 1, &readfds, &writefds, NULL, NULL);
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

            log(INFO, "New connection - FD: %d - IP: %s - Port: %d\n", new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

            // Add new socket to array of sockets and initiate a new connection to target

            for (int i = 0; i < MAX_CONNECTIONS; i++) {
                if(connections[i].src_socket == 0 ) {
                    int targetSocket = setupClientSocket(targetHost, targetPort);
                    if (targetSocket < 0) {
                        log(ERROR, "Failed to connect to target")
                    }

                    connections[i].src_socket = new_socket;
                    connections[i].dst_socket = targetSocket;

                    log(INFO, "Adding to list of sockets as %d\n" , i);
                    break;
                }
            }
                                              
        }

        // Check for available writes

        // for (int i = 0; i < MAX_CONNECTIONS; i++) {
        //     connection conn = connections[i];

        //     if (FD_ISSET(conn.dst_socket, &writefds)) {
        //         handleWrite(conn.dst_socket, conn.buffer.buffer, &writefds);
        //     }
        // }
          
        // Otherwise is some IO operation on a child socket

        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            connection con = connections[i];
              
            if (FD_ISSET(con.src_socket , &readfds)) {

                printf("OK3");

                int readBytes = read(con.src_socket, con.buffer.buffer, CONN_BUFFER);
                con.buffer.buffer[readBytes] = '\0';

                if (readBytes == 0) {
                    // Client disconnected, get his details and print

                    getpeername(con.src_socket, (struct sockaddr*) &address, (socklen_t*) &addrlen);
                    log(INFO, "Closed connection - IP: %s - Port: %d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
                        
                    // Close the client src and dest socket
                    
                    close(con.src_socket);
                    close(con.dst_socket);

                    // Remove connection from the list

                    memset((void *)(connections + i), 0, sizeof(connections[i]));
                    FD_CLR(con.src_socket, &writefds);
                } else {

                    // Prepare for writing to dst socket

                    printf("OK2");

                    FD_SET(con.dst_socket, &writefds);
                }
            }
        }
    }

}


void handleWrite(int socket, buffer *buffer, fd_set *writefds) {
    printf("OK");
    size_t bytesToSend = buffer->len - buffer->from;
    if (bytesToSend > 0) {
        log(INFO, "Trying to send %zu bytes to socket %d\n", bytesToSend, socket);

        size_t bytesSent = send(socket, buffer->buffer + buffer->from, bytesToSend, MSG_DONTWAIT);

        log(INFO, "Sent %zu bytes\n", bytesSent);

        if (bytesSent < 0) {
            log(FATAL, "Error sending to socket %d", socket);
        } else {
            size_t bytesLeft = bytesSent - bytesToSend;

            if (bytesLeft == 0) {
                buffer->from = buffer->len = 0;
                FD_CLR(socket, writefds);
            } else {
                buffer->from += bytesSent;
            }
        }
    }
}