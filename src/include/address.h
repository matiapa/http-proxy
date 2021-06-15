#ifndef UTIL_H_
#define UTIL_H_

#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <http.h>
#include <netdb.h>

#define LINK_LENGTH 100
#define PATH_LENGTH 100
#define PROTOCOL_LENGTH 6
struct url {
    char hostname[LINK_LENGTH];
    int port;
    char path[PATH_LENGTH];
    char protocol[PROTOCOL_LENGTH];
};

int sockaddr_print(const struct sockaddr *address, char * addrBuffer);

int sockaddr_equal(const struct sockaddr *addr1, const struct sockaddr *addr2);

// Gets machine FQDN if available, or unqualified hostname otherwise
int get_machine_fqdn(char * fqdn);

int parse_url(char * text, struct url * url);
/* returns 1 if it shares the ip with some interface of the proxy if not return 0*/
int is_proxy_host(const struct sockaddr * input);

#endif 
