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

//Structure for ipv4 or ipv6
union sa {
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
};

//Addrinfo structure
struct aibuf {
    struct addrinfo ai;
    union sa sa;
};


/*------------------- FUNCIONES -------------------*/
void change_to_dns_format(char* dns, const char * host);
unsigned char * get_body(unsigned char * str);
unsigned char * get_name(unsigned char * body);
char * create_post(int length, char * body);
void send_doh_request(const char * target, int s, int type);
int read_response(struct aibuf * out, int sin_port, int family, int ans_count, unsigned char * body, int initial_size);
int resolve_string(struct addrinfo ** addrinfo, const char * target, int port);

/*------------------- VARIABLES GLOBALES -------------------*/
struct doh configurations;
unsigned char buf[BUFF_SIZE];
int id = 0; // TODO: hacerlo dinamico para manejar varios pedidos doh



void initialize_doh_client(struct doh * args) {
    memcpy(&configurations, args, sizeof(struct doh));
}

void change_configuration(struct doh * args) {
    memcpy(&configurations, args, sizeof(struct doh));
}

// https://git.musl-libc.org/cgit/musl/tree/src/network/getaddrinfo.c
int doh_client(const char * target, const int sin_port, struct addrinfo ** restrict addrinfo, int family) {
    unsigned char * reader;
    struct sockaddr_in dest;
    memset(buf, 0, BUFF_SIZE);

    /*--------- Chequeo si el target esta en formato IP o es Localhost ---------*/
    if (!strcmp(target, "localhost")) return resolve_string(addrinfo, "127.0.0.1", sin_port);
    if (resolve_string(addrinfo, target, sin_port) >= 0) return 0; // El target esta en formato IP

    int s = socket(AF_INET , SOCK_STREAM , IPPROTO_TCP); // Socket para conectarse al DOH Server
    if (s < 0) {
        log(ERROR, "Socket failed to create")
        return -1;
    }

    /*--------- Establece la conexón con el servidor ---------*/
    dest.sin_family = AF_INET;
    dest.sin_port = htons(configurations.port);
    dest.sin_addr.s_addr = inet_addr(configurations.ip);

    if (connect(s, (struct sockaddr*)&dest,sizeof(dest)) < 0) { // Establece la conexión con el servidor DOH
        log(ERROR, "Error in connect")
        return -1;
    }

    struct aibuf * out = NULL;
    int cant = 0;
    int types[2] = {A, AAAA};

    for (int k = 0; k < 2; k++) {

        if (family != AF_UNSPEC && family != types[k])
            continue;

        memset(buf, 0, BUFF_SIZE); // Limpio el buffer

        /*----------- Mando el request DOH para IPv4 -----------*/
        send_doh_request(target, s, types[k]);

        /*----------- Recivo el response DOH -----------*/
        int dim = sizeof(struct sockaddr_in);
        if (recvfrom (s,(unsigned char*)buf , BUFF_SIZE , 0 , (struct sockaddr*)&dest , (socklen_t*)&dim ) < 0) {
            log(ERROR, "Getting response from DOH server")
            return -1;
        }

        unsigned char * body = get_body(buf); // TODO: cambiar esto por el parser
        struct DNS_HEADER * dns = (struct DNS_HEADER *) body; // Obtengo el DNS_HEADER
        int ans_count = ntohs(dns->ans_count); // Cantidad de respuestas
        if (out == NULL) {
            out = calloc(1, ans_count * sizeof(*out) + 1); // Se usa para llenar la estructura de las answers
        } else {
            out = realloc(out, (ans_count + cant) * sizeof(*out) + 1);
        }

        /*----------- Lectura del response DOH -----------*/
        cant = read_response(out, sin_port, family, ans_count, body, cant);
    }

    out = realloc(out, cant * sizeof(*out) + 1); // Reacomodo la memoria
    *addrinfo = &out->ai;

    return 0;
}

// TODO: borrarlo cuando pongan el parser
unsigned char * get_body(unsigned char * str) {
    int i = 0;
    int done = 1;
    int flag = 0;
    putchar('\n');
    while (done) {
        if (str[i+1] == '\n' && str[i] == '\r') {
            if (flag) done = 0;
            else flag = 1;
            i++;
        } else {
            flag = 0;
        }
        i++;
    }
    return str + i;
}

char * create_post(int length, char * body) {
    int header_count = 4;
    struct request request = {
            .method = POST,
            .header_count = header_count,
            .body_length = length
    };
    memcpy(request.body, body, length);
    strcpy(request.headers[0][0], "Accept");
    strcpy(request.headers[0][1], "application/dns-message");
    strcpy(request.headers[1][0], "Content-Type");
    strcpy(request.headers[1][1], "application/dns-message");
    strcpy(request.headers[2][0], "Host");
    snprintf(request.headers[2][1], HEADER_LENGTH, "%s:%d", configurations.host, configurations.port);
    strcpy(request.headers[3][0], "Content-Length");
    snprintf(request.headers[3][1], 4, "%d", length);
    strcpy(request.url, configurations.path);

    return create_request(&request);
}

void send_doh_request(const char * target, int s, int type) {
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

    change_to_dns_format((char *)qname, target); // Mete el host en la posición del qname

    qinfo = (struct QUESTION*)&buf[sizeof(struct DNS_HEADER) + (strlen((const char*)qname) + 1)];
    qinfo->qtype = htons(type);
    qinfo->qclass = htons(1); // Its internet

    char * string = create_post((int)(sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION)), (char *)buf); // crea el http request

    if( send(s, string, strlen(string) + sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION), 0) < 0) { // manda el paquete al servidor DOH
        log(ERROR, "Sending DOH request")
    }

    free(string); // Libera el request http
}

int read_response(struct aibuf * out, int sin_port, int family, int ans_count, unsigned char * body, int initial_size) {

    char name[30];
    unsigned char * reader = get_name(body + (int)(sizeof(struct DNS_HEADER))) + sizeof(struct QUESTION); // comienzo de las answers, me salteo la estructura QUESTION porque no me interesa

    int sin_family, cant = initial_size;
    for (int i = 0 + initial_size; i < ans_count + initial_size; i++) {
        unsigned char * after_name = get_name(reader); // TODO: ver lo de c00c
        struct R_DATA * data = (struct R_DATA *)after_name;

        if (ntohs(data->type) == A) sin_family = AF_INET;
        else if (ntohs(data->type) == AAAA) sin_family = AF_INET6;
        else sin_family = -1;

        if ((family == AF_UNSPEC && (sin_family == AF_INET || sin_family == AF_INET6)) || family == sin_family) {
            out[cant].ai = (struct addrinfo) {
                    .ai_family = sin_family,
                    .ai_socktype = SOCK_STREAM,
                    .ai_protocol = 0,
                    .ai_addrlen = sin_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
                    .ai_addr = (void *) &out[cant].sa,
                    .ai_canonname = NULL};

            if (cant) out[cant - 1].ai.ai_next = &out[cant].ai;

            switch (sin_family) {
                case AF_INET:
                    out[cant].sa.sin.sin_family = AF_INET;
                    out[cant].sa.sin.sin_port = htons(sin_port);
                    memcpy(&out[cant].sa.sin.sin_addr, ((long *) ((unsigned char *) data + sizeof(struct R_DATA))), 4);
                    break;
                case AF_INET6:
                    out[cant].sa.sin6.sin6_family = AF_INET6;
                    out[cant].sa.sin6.sin6_port = htons(sin_port);
                    out[cant].sa.sin6.sin6_scope_id = 0;
                    memcpy(&out[cant].sa.sin6.sin6_addr, ((long *) ((unsigned char *) data + sizeof(struct R_DATA))), 16);
                    break;
                default:
                    break;
            }
            cant++;
        }

        reader = (unsigned char *)(after_name + sizeof(struct R_DATA) + ntohs(data->data_len)); // acomodo al reader
    }

    return cant;
}

int resolve_string(struct addrinfo ** addrinfo, const char * target, int port) {
    struct aibuf * out = calloc(1, sizeof(*out) + 1);
    if (inet_pton(AF_INET, target, &out->sa.sin.sin_addr)) { // Es una IPv4
        out->ai = (struct addrinfo) {
                .ai_family = AF_INET,
                .ai_socktype = SOCK_STREAM,
                .ai_protocol = 0,
                .ai_addrlen = sizeof(struct sockaddr_in),
                .ai_addr = (void *) &out->sa,
                .ai_canonname = NULL,
        };

        out->sa.sin.sin_family = AF_INET;
        out->sa.sin.sin_port = htons(port);
        *addrinfo = &out->ai;
        return 0;
    } else if (inet_pton(AF_INET6, target, &out->sa.sin6.sin6_addr)) { // Es una IPv6
        out->ai = (struct addrinfo) {
                .ai_family = AF_INET6,
                .ai_socktype = SOCK_STREAM,
                .ai_protocol = 0,
                .ai_addrlen = sizeof(struct sockaddr_in6),
                .ai_addr = (void *) &out->sa,
                .ai_canonname = NULL,
        };

        out->sa.sin6.sin6_family = AF_INET6;
        out->sa.sin6.sin6_port = htons(port);
        out->sa.sin6.sin6_scope_id = 0;
        *addrinfo = &out->ai;
        return 0;
    } else {
        return -1; // Es una URL
    }
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

unsigned char * get_name(unsigned char * body) {
    if (*body & 0xC0) { // Chequeo si no esta en formato comprimido
        return body + 2;
    }

    body++; // para evitar el primer punto
    while (*((char *)body) != 0) {
        body++;
    }

    return body + 1;
}
