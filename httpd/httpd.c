#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/time.h>
#include "../include/logger.h"
#include "../include/io.h"
#include "../include/server.h"

#define BUFF_SIZE 1025
#define MAX_CLIENTS 500

void conn_handler(int new_socket);

int io_handler(int sd);



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

    handleConnections(passiveSocket, NULL, NULL);
      
    return 0;

}