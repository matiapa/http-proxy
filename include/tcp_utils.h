#ifndef TCP_UTILS_H_
#define TCP_UTILS_H_

#include <selector.h>

int create_tcp_client(const char *host, const int port);

int create_tcp_server(const char *port);

int handle_connections(int server, void (*handle_creates) (struct selector_key *key));

#endif 
