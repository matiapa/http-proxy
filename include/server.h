#ifndef TCPSERVERUTIL_H_
#define TCPSERVERUTIL_H_

#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>
#include "./buffer.h"

#define CONN_BUFFER 1024
#define MAX_CLIENTS 500
#define MAX_CONNECTIONS MAX_CLIENTS * 2 + 1


typedef struct connection {
    int src_socket;
    int dst_socket;

    buffer src_dst_buffer;
    buffer dst_src_buffer;
} connection;

char *targetHost, *targetPort;

// Create, bind, and listen a new TCP server socket
int setupServerSocket(const char *service);

// Accept a new TCP connection on a server socket
void handleConnections(int master_socket, int (*read_handler)(int), int (*write_handler)(int));

#endif 
