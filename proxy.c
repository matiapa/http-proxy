#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/time.h>
#include "./include/logger.h"
#include "./include/io.h"
#include "./include/client.h"
#include "./include/server.h"

#define BUFF_SIZE 1025
#define MAX_CLIENTS 500

void conn_handler(int new_socket);

int io_handler(int sd);


char *targetHost, *targetPort;

int targetSockets[MAX_CLIENTS] = {0};


int main(int argc , char *argv[]) {

    if(argc != 4){
        printf("Usage: %s <LISTEN_PORT> <TARGET_HOST> <TARGET_PORT>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *listenPort = argv[1];
    targetHost = argv[2];
    targetPort = argv[3];

    // Start accepting connections
      
    int passiveSocket = setupServerSocket(listenPort);

    handleClients(passiveSocket, conn_handler, io_handler);
      
    return 0;

}


void conn_handler(int clientSocket){

    // Create a new connection to target

    ssend(clientSocket, "Hello! Proxy at your service\n");

    int targetSocket = tcpClientSocket(targetHost, targetPort);
	if (targetSocket < 0) {
        ssend(clientSocket, "Oh no! Couldn't connect to target\n");
		log(FATAL, "Failed to connect to target")
		exit(EXIT_FAILURE);
	}

    ssend(clientSocket, "Connection to target established\n");
    targetSockets[clientSocket] = targetSocket;

}


int io_handler(int clientSocket){

    // Redirect client message to target

    char buffer[BUFF_SIZE];

    int readBytes = read(clientSocket, buffer, 1024);
    if (readBytes == 0)
        return 0;

    buffer[readBytes] = '\0';
    ssend(targetSockets[clientSocket], buffer);

    return readBytes;

}