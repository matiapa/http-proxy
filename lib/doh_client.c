#include <doh_client.h>
#include <http_request_factory.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h> //printf

void change_to_dns_format(char* dns, char* host);

char * server_ip = "172.16.82.14";
int id = 0;

//DNS header structure
struct DNS_HEADER {
    unsigned short id; // identification number

    unsigned char rd :1; // recursion desired
    unsigned char tc :1; // truncated message
    unsigned char aa :1; // authoritive answer
    unsigned char opcode :4; // purpose of message
    unsigned char qr :1; // query/response flag

    unsigned char rcode :4; // response code
    unsigned char cd :1; // checking disabled
    unsigned char ad :1; // authenticated data
    unsigned char z :1; // its z! reserved
    unsigned char ra :1; // recursion available

    unsigned short q_count; // number of question entries
    unsigned short ans_count; // number of answer entries
    unsigned short auth_count; // number of authority entries
    unsigned short add_count; // number of resource entries
};

//Constant sized fields of query structure
struct QUESTION
{
    unsigned short qtype;
    unsigned short qclass;
};

//Constant sized fields of the resource record structure
#pragma pack(push, 1)
struct R_DATA
{
    unsigned short type;
    unsigned short _class;
    unsigned int ttl;
    unsigned short data_len;
};
#pragma pack(pop)

//Pointers to resource record contents
struct RES_RECORD
{
    unsigned char *name;
    struct R_DATA *resource;
    unsigned char *rdata;
};

//Structure of a Query
typedef struct
{
    unsigned char *name;
    struct QUESTION *ques;
} QUERY;

void fillStruct() {
    char buf[65536],*qname,*reader;

    struct DNS_HEADER *dns;
    struct QUESTION *qinfo = NULL;

    dns = (struct DNS_HEADER *)&buf;
    dns->id = id;
    dns->qr = 0; //This is a query
    dns->opcode = 0; //This is a standard query
    dns->aa = 0; //Not Authoritative
    dns->tc = 0; //This message is not truncated
    dns->rd = 1; //Recursion Desired
    dns->ra = 0; //Recursion not available! hey we dont have it (lol)
    dns->z = 0;
    dns->ad = 0;
    dns->cd = 0;
    dns->rcode = 0;
    dns->q_count = htons(1); //we have only 1 question
    dns->ans_count = 0;
    dns->auth_count = 0;
    dns->add_count = 0;

    char * host = "www.example.com";
    qname = (char*)&buf[sizeof(struct DNS_HEADER)];
    change_to_dns_format(qname, host);

    qinfo =(struct QUESTION*)&buf[sizeof(struct DNS_HEADER) + (strlen((const char*)qname) + 1)]; //fill it

    qinfo->qtype = htons(1); //type of the query , A , MX , CNAME , NS etc
    qinfo->qclass = htons(1); //its internet (lol)

    int header_count = 3;
    char *** headers = malloc(sizeof(char *)*HEADER_COLUMNS*header_count);
    char * header1[2] = {"Accept", "application/dns-message"};
    char * header2[2] = {"Content-Type", "application/dns-message"};
    char * header3[3] = {"Content-Length", "33"};
    headers[0] = header1;
    headers[1] = header2;
    headers[2] = header3;
    struct request request = {
            .method = POST,
            .body = buf,
            .headers = headers,
            .header_count = header_count,
            .file = "dns-query"
    };
    char * string = request_factory(&request);
    char * aux = malloc(200);
    memcpy(aux, string, strlen(string));
    memcpy(aux + strlen(string), buf, sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION));
    aux[strlen(string) + sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION)] = '\0';
    printf("%s\n", string);
    free(headers);

    printf("Resolving %s", host);

    struct sockaddr_in dest;
    int s = socket(AF_INET , SOCK_STREAM , IPPROTO_TCP); //UDP packet for DNS queries

    dest.sin_family = AF_INET;
    dest.sin_port = htons(80);
    dest.sin_addr.s_addr = inet_addr(server_ip); //dns servers

    if (connect(s, (struct sockaddr*)&dest,sizeof(dest)) < 0) {
        perror("error in connect");
    }

    printf("\nSending Packet...");
    printf("%d\n", (int)(strlen(string) + sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION)));
    if( send(s,(char*)aux, strlen(string) + sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION),0) < 0) {
        perror("sendto failed");
    }
    printf("Done");
    free(string);
}

void change_to_dns_format(char* dns, char* host) {
    int len = (int)strlen(host);
    memcpy(dns, ".", 1);
    memcpy(dns + 1, host, len);

    int cant = 0;
    for (int i = len; i >= 0; i--) {
        if (dns[i] != '.') {
            cant++;
        } else {
            dns[i] = cant;
            cant = 0;
        }
    }
}
