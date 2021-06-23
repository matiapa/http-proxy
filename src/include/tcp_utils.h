#ifndef TCP_UTILS_H_
#define TCP_UTILS_H_

#include <selector.h>

extern fd_selector selector_fd;

/* Tries to establish a TCP connection with a host
*  Returns client socket or an error code if faile:
*  -1: Internal error | -2: Host is offline
*  -3: Host is the proxy | -4: Unable to resolve address
*/
int create_tcp_client(const char *host, const int port);

int create_tcp6_server(const char *address, const char *port);

int create_tcp_server(const char *address, const char *port);

int handle_connections(int master_sockets[MASTER_SOCKET_SIZE], int udp_sockets[MASTER_SOCKET_SIZE], void (*handle_creates)(struct selector_key *key));

#endif 
