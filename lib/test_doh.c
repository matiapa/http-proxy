#include <doh_client.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

int main(int argc, char **argv) {

    struct addrinformation addrCriteria;
    memset(&addrCriteria, 0, sizeof(addrCriteria));

    addrCriteria.ai_family = AF_INET;
    addrCriteria.ai_socktype = SOCK_STREAM;
    addrCriteria.ai_protocol = IPPROTO_TCP;

    // Resolve host string for posible addresses

    struct addrinformation *servAddr;

    char * port = "8080";
    fillStruct();
}
