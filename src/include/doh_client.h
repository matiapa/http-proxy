#ifndef PC_2021A_06_DOH_CLIENT_H
#define PC_2021A_06_DOH_CLIENT_H

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <args.h>
#include <sys/time.h>

void initialize_doh_client(struct doh * args);

int doh_client(const char * target, const int port, struct addrinfo ** restrict addrinfo, int family);

#endif //PC_2021A_06_DOH_CLIENT_H
