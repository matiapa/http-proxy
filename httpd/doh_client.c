#include <doh_client.h>
#include <http.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h> //printf
#include <args.h>
#include <logger.h>

#define A 1
#define AAAA 28
#define ANY 255
#define BUFF_SIZE 6000

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

    uint16_t q_count; // number of question entries
    uint16_t ans_count; // number of answer entries
    uint16_t auth_count; // number of authority entries
    uint16_t add_count; // number of resource entries
};

//Constant sized fields of query structure
struct QUESTION {
    uint16_t qtype;
    uint16_t qclass;
};

//Constant sized fields of the resource record structure
#pragma pack(push, 1)
struct R_DATA {
    uint16_t type;
    uint16_t _class;
    uint32_t ttl;
    uint16_t data_len;
};
#pragma pack(pop)

struct IPV6 {
    uint8_t nums[16];
};


void change_to_dns_format(char* dns, const char * host);
unsigned char * get_body(unsigned char * buf);
unsigned char * get_name(char * buf, unsigned char * body);
char * create_post(int length, char * body);
void send_doh_request(const char * target, int s, int family);
void resolve_localhost(struct addrinfo * addr, const char * port);
void read_answer(struct R_DATA * data, struct addrinfo * answer, const char * port, int family);

struct doh configurations;
struct sockaddr_in dest;
int id = 0; // TODO: hacerlo dinamico para manejar
unsigned char buf[BUFF_SIZE];
unsigned char * reader;

void initialize_doh_client(struct doh * args) {
    memcpy(&configurations, args, sizeof(struct doh));
}

int doh_client(const char * target, const char * port, struct addrinfo * addrinfo, int family) {

    if (!strcmp(target, "localhost")) {
        resolve_localhost(addrinfo, port);
        return 0;
    }

    int s = socket(AF_INET , SOCK_STREAM , IPPROTO_TCP); //TCP packet for DNS queries
    if (s < 0) {
        log(ERROR, "Socket failed to create")
        return -1;
    }

    /* Establece la conexón con el servidor */
    dest.sin_family = AF_UNSPEC;
    dest.sin_port = htons(configurations.port); // el servidor DOH esta en localhost:8053
    dest.sin_addr.s_addr = inet_addr(configurations.ip); //dns servers, esto deberia ser variable al igual que el port

    if (connect(s, (struct sockaddr*)&dest,sizeof(dest)) < 0) { // establece la conexión con el servidor DOH
        log(ERROR, "Error in connect")
        return -1;
    }

    /* Mando el request DOH */
    send_doh_request(target, s, AF_INET);

    struct DNS_HEADER * dns;

    /* Recivo el response DOH */
    int dim = sizeof(struct sockaddr_in);
    memset(buf, 0, BUFF_SIZE);
    if (recvfrom (s,(unsigned char*)buf , BUFF_SIZE , 0 , (struct sockaddr*)&dest , (socklen_t*)&dim ) < 0) { // se recive el response HTTP
        log(ERROR, "reading response failed")
        return -1;
    }

    /* Comienza la lectura del response HTTP */
    unsigned char * body = get_body(buf); // TODO: cambiar esto por el parser
    dns = (struct DNS_HEADER *) body; // obtengo el DNS_HEADER

    char name[20];
    unsigned char * dataDir = get_name(name, body + (int)(sizeof(struct DNS_HEADER))); // obtengo el nombre del question, reemplazo los '.'

    reader = dataDir + 1 + sizeof(struct QUESTION); // comienzo de las Answers, me salteo la estructura QUESTION porque no me interesa

    /* Comienza la lectura de las respuestas */

    int ans_count = ntohs(dns->ans_count); // cantidad de respuestas

    struct addrinfo * answer = addrinfo; // lo uso para iterar
    for (int i = 0; i < ans_count; i++) {
        unsigned char * after_name = get_name(name, reader) + 1;

        read_answer((struct R_DATA *)after_name, answer, port, family); // leo la answer

        if (answer->ai_family == AF_INET)
            reader = (unsigned char *)(after_name + sizeof(struct R_DATA) + sizeof(answer->ai_addr->sa_data)); // acomodo al reader

        if (answer->ai_family == AF_INET6)
            reader = (unsigned char *)(after_name + sizeof(struct R_DATA) + sizeof(struct IPV6)); // acomodo al reader

        // asigno el siguiente nodo en caso de que no sea el último
        if (answer->ai_family != AF_INET && answer->ai_addr->sa_family != AF_INET6) {
            break;
        }

        if (i != ans_count - 1) {
            answer->ai_next = (struct addrinfo *)malloc(sizeof(struct addrinfo));
            answer = answer->ai_next;
        }
    }
    answer->ai_next = NULL;

    return 0;
}

void change_to_dns_format(char* dns, const char * host) {
    int len = (int)strlen(host);
    memcpy(dns, ".", 1);
    memcpy(dns + 1, host, len);

    char cant = 0;
    for (int i = (int)strlen(dns) - 1; i >= 0; i--) {
        if (dns[i] != '.') {
            cant++;
        } else {
            dns[i] = cant;
            cant = 0;
        }
    }
    dns[len + 1] = 0;
}

// TODO: borrarlo cuando pongan el parser
unsigned char * get_body(unsigned char * buf) {
    int i = 0;
    int done = 1;
    int flag = 0;
    putchar('\n');
    while (done) {
        if (buf[i+1] == '\n' && buf[i] == '\r') {
            if (flag) done = 0;
            else flag = 1;
            i++;
        } else {
            flag = 0;
        }
        i++;
    }
    return buf + i;
}

unsigned char * get_name(char * buf, unsigned char * body) {
    char c;
    int pos = 0;
    body++; // para evitar el primer punto
    while ((c = *((char *)body)) != '\0') {
        if (c < 'a') buf[pos++] = '.';
        else buf[pos++] = c;
        body++;
    }
    buf[pos] = '\0';
    return body;
}

char * create_post(int length, char * body) {
    int header_count = 4;
    char *** headers = malloc(sizeof(char *)*HEADER_COLUMNS*header_count);
    char * header1[2] = {"Accept", "application/dns-message"};
    char * header2[2] = {"Content-Type", "application/dns-message"};
    char * header3[2] = {"Content-Length", 0};
    header3[1] = malloc(4);
    snprintf(header3[1], 4, "%d", length);
    char host[100];
    snprintf(host, 100, "%s:%d", configurations.host, configurations.port);
    char * header4[2] = {"Host", host};
    headers[0] = header1;
    headers[1] = header2;
    headers[2] = header3;
    headers[3] = header4;
    struct request request = {
            .method = POST,
            .body = body,
            .headers = headers,
            .header_count = header_count,
            .file = configurations.path,
            .body_size = length
    };

    char * string = create_request(&request);

    free(header3[1]);
    free(headers);

    return string;
}

void send_doh_request(const char * target, int s, int family) {
    unsigned char * qname;
    struct QUESTION * qinfo = NULL;

    struct DNS_HEADER *dns;
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

    qname = (unsigned char*)&buf[sizeof(struct DNS_HEADER)];

    change_to_dns_format((char *)qname, target); // mete el host en la posición del qname

    qinfo = (struct QUESTION*)&buf[sizeof(struct DNS_HEADER) + (strlen((const char*)qname) + 1)]; //fill it

    qinfo->qtype = htons(ANY);

    qinfo->qclass = htons(1); //its internet (lol)

    char * string = create_post((int)(sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION)), (char *)buf); // crea el http request

    if( send(s, string, strlen(string) + sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION), 0) < 0) { // manda el paquete al servidor DOH
        log(ERROR, "Sending DOH request")
    }

    free(string); // libera el request http
}

void read_answer(struct R_DATA * data, struct addrinfo * answer, const char * port, int family) {
    answer->ai_socktype = SOCK_STREAM;
    answer->ai_protocol = 0;

    if (family == AF_INET && ntohs(data->type) == A) { // si es IPV4
        answer->ai_addr = malloc(sizeof(struct sockaddr_in));
        struct sockaddr_in * addr_in = (struct sockaddr_in *) answer->ai_addr;
        answer->ai_family = AF_INET;
        answer->ai_addrlen = sizeof(struct sockaddr_in);

        addr_in->sin_family = AF_INET;
        addr_in->sin_port = htons(atoi(port));

        memcpy(&(addr_in->sin_addr), &((*((long*)((unsigned char *)data + sizeof(struct R_DATA))))), INET_ADDRSTRLEN);

    } else if (family == AF_INET6 && ntohs(data->type) == AAAA) { // si es IPV6
        answer->ai_addr = malloc(sizeof(struct sockaddr_in6));
        struct sockaddr_in6 * addr_in = (struct sockaddr_in6 *) answer->ai_addr;

        answer->ai_family = AF_INET6;
        answer->ai_addrlen = sizeof(struct sockaddr_in6);

        addr_in->sin6_family = AF_INET6;
        addr_in->sin6_port = htons(atoi(port));

        memcpy(&(addr_in->sin6_addr), &((*((long*)((unsigned char *)data + sizeof(struct R_DATA))))), INET6_ADDRSTRLEN);

    }
}

void resolve_localhost(struct addrinfo * addrinfo, const char * port) {
    addrinfo->ai_addr = malloc(sizeof(struct sockaddr_in));
    int num = ntohs(atoi(port));
    memcpy(&(addrinfo->ai_addr->sa_data), &num, sizeof(port));
    inet_pton(AF_INET, "127.0.0.1", &(addrinfo->ai_addr->sa_data[2]));
    addrinfo->ai_socktype = SOCK_STREAM;
    addrinfo->ai_protocol = IPPROTO_TCP;
    addrinfo->ai_family = AF_INET;
    addrinfo->ai_addrlen = INET_ADDRSTRLEN;
    addrinfo->ai_addr->sa_family = AF_INET;
    addrinfo->ai_addr->sa_len = INET_ADDRSTRLEN;
    addrinfo->ai_next = NULL;
}

void freeaddresses(struct addrinfo * addrinfo) {
    struct addrinfo * tmp;

    while (addrinfo != NULL) {
        tmp = addrinfo;
        addrinfo = addrinfo->ai_next;
        free(tmp->ai_addr);
        free(tmp);
    }
}

