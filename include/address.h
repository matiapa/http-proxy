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

int printSocketAddress(const struct sockaddr *address, char * addrBuffer);

const char * printFamily(struct addrinfo *aip);

const char * printType(struct addrinfo *aip);

const char * printProtocol(struct addrinfo *aip);

void printFlags(struct addrinfo *aip);

char * printAddressPort( const struct addrinfo *aip, char addr[]);

// Gets machine FQDN if available, or unqualified hostname otherwise
int get_machine_fqdn(char * fqdn);

int parse_url(char * text, struct url * url);

// Determina si dos sockets son iguales (misma direccion y puerto)
int sockAddrsEqual(const struct sockaddr *addr1, const struct sockaddr *addr2);

#endif 
