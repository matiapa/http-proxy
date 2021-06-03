#ifndef PC_2021A_06_DOH_CLIENT_H
#define PC_2021A_06_DOH_CLIENT_H

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

struct addrinformation {
    int                     ai_flags;
    int                     ai_family;
    int                     ai_socktype;
    int                     ai_protocol;
    socklen_t               ai_addrlen;
    struct sockaddr        *ai_addr;
    char                   *ai_canonname;
    struct addrinformation *ai_next;
};

int getdnsinfo(const char *restrict node,
               const char *restrict service,
               const struct addrinformation *restrict hints,
               struct addrinformation **restrict res);

void fillStruct();

#endif //PC_2021A_06_DOH_CLIENT_H