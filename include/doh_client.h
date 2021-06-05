#ifndef PC_2021A_06_DOH_CLIENT_H
#define PC_2021A_06_DOH_CLIENT_H

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

#define __packed __attribute__((packed))

struct qname {
    uint8_t len;
    uint8_t label[8];
} __packed;

struct __packed root {
    struct __packed {
        uint8_t len;        // 5
        uint8_t data[5];    // 'r' 'b' 'n' 'd' 'r'
    } domain;
    struct __packed {
        uint8_t len;        // 2
        uint8_t data[2];    // 'u' 's'
    } tld;
    uint8_t root;           // 0
};

static const struct root kExpectedDomain = {
        .domain = { 5, { 'r', 'b', 'n', 'd', 'r' } },
        .tld    = { 2, { 'u', 's' } },
        .root   = 0,
};

struct __packed header {
    uint16_t id;
    struct __packed {
        unsigned  rd      : 1;
        unsigned  tc      : 1;
        unsigned  aa      : 1;
        unsigned  opcode  : 4;
        unsigned  qr      : 1;
        unsigned  rcode   : 4;
        unsigned  ra      : 1;
        unsigned  ad      : 1;
        unsigned  z       : 2;
    } flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
    struct __packed {
        struct qname primary;
        struct qname secondary;
        struct root  domain;
    } labels;
    uint16_t qtype;
    uint16_t qclass;
    struct __packed {
        uint8_t flag;
        uint8_t offset;
    } ptr;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
    struct in_addr rdata;
} __packed;

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
