#ifndef TCPSERVERUTIL_H_
#define TCPSERVERUTIL_H_

#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>

#define CONN_BUFFER 1024
#define MAX_CLIENTS 500
#define MAX_CONNECTIONS MAX_CLIENTS * 2 + 1

typedef struct buffer {
    char buffer[CONN_BUFFER];
    size_t len;
    size_t from;    // Last write position
} buffer;

typedef enum conn_type { CLIENT, SERVER } conn_type;
typedef struct connection {
    int src_socket;
    int dst_socket;

    buffer buffer;

    conn_type conn_type;
} connection;

char *targetHost, *targetPort;

// Should allow for two sockets per client (for client and target) and the passive socket
static connection connections[MAX_CONNECTIONS] = {0};
static int connectionsSize = 0;

// Create, bind, and listen a new TCP server socket
int setupServerSocket(const char *service);

// Accept a new TCP connection on a server socket
void handleConnections(int master_socket, int (*read_handler)(int), int (*write_handler)(int));

#endif 
