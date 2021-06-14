// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <address.h>
#include <logger.h>
#include <udp_utils.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define ADDR_BUFF_SIZE 128

int create_udp_server(const char *port)
{

    struct addrinfo addrCriteria;
    memset(&addrCriteria, 0, sizeof(addrCriteria));

    addrCriteria.ai_family = AF_INET;
    addrCriteria.ai_flags = AI_PASSIVE; // Accept on any address/port
    addrCriteria.ai_socktype = SOCK_DGRAM;
    addrCriteria.ai_protocol = IPPROTO_UDP;

    // Try to bind to an address and to start listening on it

    int servSock = -1;
    struct sockaddr_in serveraddr;
    servSock = socket(addrCriteria.ai_family, addrCriteria.ai_socktype, addrCriteria.ai_protocol);
    if (servSock < 0)
    {
        log(ERROR, "Creating passive socket");
        return -1;
    }
    log(DEBUG, "UDP socket %d created", servSock);
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    if (setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, (char *)&timeout, sizeof(int)) < 0)
    {
        log(ERROR, "set IPv4 socket options SO_REUSEADDR failed %s ", strerror(errno));
        return -1;
    }

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(atoi(port));

    inet_pton(serveraddr.sin_family, "127.0.0.1", &serveraddr.sin_addr.s_addr);

    // Bind to address

    if (bind(servSock, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
    {
        log(ERROR, "bind for IPv4 failed");
        close(servSock);
        return -1;
    }

    // Imprimir dirección
    struct sockaddr_storage localAddr;
    socklen_t addrSize = sizeof(localAddr);

    int getname = getsockname(servSock, (struct sockaddr *)&localAddr, &addrSize);
    if (getname < 0)
    {
        log(ERROR, "Getting master socket name");
        return -1;
    }

    char addressBuffer[ADDR_BUFF_SIZE];
    printSocketAddress((struct sockaddr *)&localAddr, addressBuffer);

    log(INFO, "Binding to %s", addressBuffer);

    return servSock;
}

int create_udp6_server(const char *port)
{

    // Create address criteria

    struct addrinfo addrCriteria;
    memset(&addrCriteria, 0, sizeof(addrCriteria));

    addrCriteria.ai_family = AF_INET6;
    addrCriteria.ai_flags = AI_PASSIVE; // Accept on any address/port
    addrCriteria.ai_socktype = SOCK_DGRAM;
    addrCriteria.ai_protocol = IPPROTO_UDP;

    // Try to bind to an address and to start listening on it

    int servSock = -1;
    struct sockaddr_in6 server6addr;
    servSock = socket(addrCriteria.ai_family, addrCriteria.ai_socktype, addrCriteria.ai_protocol);
    if (servSock < 0)
    {
        log(ERROR, "Creating passive socket");
        return -1;
    }
    log(DEBUG, "UDP socket %d created", servSock);

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    if (setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, (char *)&timeout, sizeof(int)) < 0)
    {
        log(ERROR, "set IPv6 socket options SO_REUSEADDR failed %s ", strerror(errno));
    }
    if (setsockopt(servSock, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&timeout, sizeof(int)) < 0)
    {
        log(ERROR, "set IPv6 socket options IPV6_V6ONLY failed %s ", strerror(errno));
    }

    memset(&server6addr, 0, sizeof(server6addr));
    server6addr.sin6_family = AF_INET6;
    server6addr.sin6_port = htons(atoi(port));
    server6addr.sin6_addr = in6addr_loopback;
    // Bind to address

    if (bind(servSock, (struct sockaddr *)&server6addr, sizeof(server6addr)) < 0) { //-V641
        log(ERROR, "bind for IPv6 failed");
        close(servSock);
        return -1;
    }

    struct sockaddr_storage localAddr;
    socklen_t addrSize = sizeof(localAddr);

    int getname = getsockname(servSock, (struct sockaddr *)&localAddr, &addrSize);
    if (getname < 0)
    {
        log(ERROR, "Getting master socket name");
        return -1;
    }

    char addressBuffer[ADDR_BUFF_SIZE];
    printSocketAddress((struct sockaddr *)&localAddr, addressBuffer);

    log(INFO, "Binding to %s", addressBuffer);

    return servSock;
}

ssize_t uread(int fd, char *buffer, size_t buffSize, struct sockaddr *address, socklen_t *addressSize)
{
    char addrBuffer[ADDR_BUFF_SIZE] = {0};

    ssize_t recvBytes = recvfrom(fd, buffer, buffSize, 0, address, addressSize);
    if (recvBytes < 0)
    {
        log(ERROR, "Recieving bytes: %s ", strerror(errno)) return -1;
    }

    printSocketAddress((struct sockaddr *)address, addrBuffer);
    log(INFO, "Handling client %s - Received %ld bytes", addrBuffer, recvBytes);

    return recvBytes;
}

ssize_t usend(int fd, char *buffer, size_t buffSize, struct sockaddr *address, socklen_t addressSize)
{
    char addrBuffer[ADDR_BUFF_SIZE] = {0};

    ssize_t sentBytes = sendto(fd, buffer, buffSize, 0, address, addressSize);
    if (sentBytes < 0)
    {
        log(ERROR, "Sending bytes: %s ", strerror(errno)) return -1;
    }

    printSocketAddress((struct sockaddr *)address, addrBuffer);
    log(DEBUG, "Handling client %s - Sent %ld bytes", addrBuffer, sentBytes);

    return sentBytes;
}
