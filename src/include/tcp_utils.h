#ifndef TCP_UTILS_H_
#define TCP_UTILS_H_

#include <selector.h>

int create_tcp6_server(const char *address, const char *port);

int create_tcp_server(const char *address, const char *port);

int handle_passive_sockets(int sockets[], fd_handler ** handlers, int size);

#endif 
