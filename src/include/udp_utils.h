#ifndef UDP_SERVER_UTILS_H_
#define UDP_SERVER_UTILS_H_

#include <netdb.h>

int create_udp_server(const char *address, unsigned short port);

int create_udp6_server(const char *address, unsigned short port);

ssize_t uread(int fd, char * buffer, size_t buffSize, struct sockaddr * address, socklen_t * addressSize);

ssize_t usend(int fd, char * buffer, size_t buffSize, struct sockaddr * address, socklen_t addressSize);

#endif 
