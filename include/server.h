#ifndef TCPSERVERUTIL_H_
#define TCPSERVERUTIL_H_

#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>

// Create, bind, and listen a new TCP server socket
int setupServerSocket(const char *service);

// Accept a new TCP connection on a server socket
void handleClients(int master_socket, void (*conn_handler)(int), int (*io_handler)(int));

#endif 
