#ifndef TCP_SERVER_UTILS_H_
#define TCP_SERVER_UTILS_H_

#include <selector.h>

char *targetHost, *targetPort;

// Create, bind, and listen a new TCP server socket
int create_tcp_server(const char *port);

// Accept a new TCP connection on a server socket
int handle_connections(
    int server,
    void (*handle_creates) (struct selector_key *key),
    void (*handle_reads) (struct selector_key *key),
    void (*handle_writes) (struct selector_key *key)
);

#endif 
