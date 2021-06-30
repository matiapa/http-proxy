#include <http.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <proxy_args.h>
#include <logger.h>
#include <buffer.h>
#include <http_response_parser.h>
#include <string.h>
#include <errno.h>
#include <proxy.h>
#include <doh_client.h>

#define A 1
#define AAAA 28
#define BUFF_SIZE 6000
#define POST_SIZE 1024

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
int change_to_dns_format(char* dns, const char * host);
int get_name(unsigned char * body);
int create_post(int length, char * body, char * write_buffer, int space);
int read_response(struct aibuf * out, int sin_port, int family, int ans_count, struct buffer buff);

/*------------------- VARIABLES GLOBALES -------------------*/
struct doh configurations;
int id = 0;


void config_doh_client(struct doh * args) {
    memcpy(&configurations, args, sizeof(struct doh));
}


int doh_client_init(selector_key_t * key) {

    // Create DoH client socket and set O_NONBLOCK

    int s = socket(AF_INET , SOCK_STREAM , IPPROTO_TCP);
    if (s < 0) {
        log(ERROR, "Failed to create DoH client socket")
        return -1;
    }

    selector_fd_set_nio(s);

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&timeout, sizeof(int)) < 0) {
        log(ERROR, "Failed to set DoH client socket options SO_REUSEADDR: %s ", strerror(errno))
    }

    // Register socket at selector

    extern const struct fd_handler proxy_handlers;

    if(selector_register(key->s, s, &proxy_handlers, OP_WRITE, key->data) != SELECTOR_SUCCESS) {
        log(ERROR, "Failed to register DoH client socket %d at selector", s);
        return -1;
    }

    // Set up item elements

    buffer_init(&(I(key)->doh.buff), BUFF_SIZE, calloc(1, BUFF_SIZE));

    I(key)->doh.server_socket = -1;

    // Connect to server

    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(configurations.port);
    dest.sin_addr.s_addr = inet_addr(configurations.ip);

    if (connect(s, (struct sockaddr *)&dest, sizeof(dest)) == -1) {
        if (errno == EINPROGRESS) {
            I(key)->doh.server_socket = s;
            I(key)->doh.family = AF_INET;   // Set to IPv4 first
            I(key)->references++;
            return 0;
        } else {
            log(ERROR, "Failed to initiate connection to DoH server")
            doh_kill(key);
            return -1;
        }
    } else {
        abort();
    }
}


void doh_kill(selector_key_t * key) {
    selector_unregister_fd(key->s, I(key)->doh.server_socket);
    close(I(key)->doh.server_socket);

    if (I(key)->doh.target_address_list != NULL)
        free(I(key)->doh.target_address_list);
    
    free_buffer(&(I(key)->doh.buff));
    memset(&(I(key)->doh), 0, sizeof(struct doh_client));
}


int doh_client_read(selector_key_t * key) {
    /*----------- Recivo el response DOH -----------*/
    int dim = sizeof(struct sockaddr_in);
    size_t nbyte;
    buffer_reset(&(I(key)->doh.buff));
    memset(I(key)->doh.buff.read, 0, BUFF_SIZE);
    char *aux_buff = (char *)buffer_write_ptr(&(I(key)->doh.buff), &nbyte);

    ssize_t read_bytes;
    struct sockaddr_in dest;
    if ((read_bytes = recvfrom(I(key)->doh.server_socket, aux_buff, nbyte, 0, (struct sockaddr *)&dest, (socklen_t *)&dim)) < 0) {
        log(ERROR, "Getting response from DOH server")
        free_buffer(&(I(key)->doh.buff));
        close(I(key)->doh.server_socket);
        return -1;
    }

    buffer_write_adv(&(I(key)->doh.buff), read_bytes);

    // Parsing of response
    http_response_parser parser = {0};
    http_response_parser_init(&parser);
    http_response_parser_parse(&parser, &(I(key)->doh.buff), false); // response is parsed
    http_response response = parser.response;

    I(key)->doh.buff.read = (unsigned char *)response.message.body;

    struct DNS_HEADER *dns = calloc(1, sizeof(struct DNS_HEADER));
    if (dns == NULL) {
        log(ERROR, "Doing calloc of dns header")
        return -1;
    }

    memcpy(dns, buffer_read_ptr(&(I(key)->doh.buff), &nbyte), sizeof(struct DNS_HEADER)); // Obtengo el DNS_HEADER
    buffer_read_adv(&(I(key)->doh.buff), sizeof(struct DNS_HEADER));

    int ans_count = ntohs(dns->ans_count); // Cantidad de respuestas
    free(dns);

    if (ans_count == 0) {
        return 0;
    }

    struct aibuf * out = calloc(1, ans_count * sizeof(*out) + 1); // Se usa para llenar la estructura de las answers
    if (out == NULL) {
        log(ERROR, "Doing calloc of out")
        return -1;
    }

    int n = get_name(buffer_read_ptr(&(I(key)->doh.buff), &nbyte)) + sizeof(struct QUESTION);
    buffer_read_adv(&(I(key)->doh.buff), n); // comienzo de las answers, me salteo la estructura QUESTION porque no me interesa

    /*----------- Lectura del response DOH -----------*/
    ans_count = read_response(out, I(key)->target_url.port, I(key)->doh.family, ans_count, I(key)->doh.buff);

    // Termine
    I(key)->doh.target_address_list = &out->ai;
    I(key)->doh.current_target_addr = I(key)->doh.target_address_list;
    return ans_count;
}


int create_post(int length, char * body, char * write_buffer, int space) {
    int header_count = 4;
    http_request request = {
        .method = POST,
        .message.header_count = header_count,
        .message.body_length = length,
        .message.body = body
    };
    strcpy(request.message.headers[0][0], "Accept");
    strcpy(request.message.headers[0][1], "application/dns-message");
    strcpy(request.message.headers[1][0], "Content-Type");
    strcpy(request.message.headers[1][1], "application/dns-message");
    strcpy(request.message.headers[2][0], "Host");
    snprintf(request.message.headers[2][1], HEADER_LENGTH, "%s:%d", configurations.host, configurations.port);
    strcpy(request.message.headers[3][0], "Content-Length");
    snprintf(request.message.headers[3][1], 4, "%d", length);
    strcpy(request.url, configurations.path);

    return write_request(&request, write_buffer, space, true);
}


int send_doh_request(selector_key_t * key, int type) {
    buffer_reset(&(I(key)->doh.buff));
    memset(I(key)->doh.buff.write, 0, BUFF_SIZE);

    unsigned char * qname;
    struct QUESTION * qinfo = NULL;
    size_t nbyte;

    struct DNS_HEADER *dns;
    dns = (struct DNS_HEADER *)(char *)buffer_write_ptr(&(I(key)->doh.buff), &nbyte);
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
    buffer_write_adv(&(I(key)->doh.buff), sizeof(struct DNS_HEADER)); // muevo el buffer despues de haber escrito DNS_HEADER

    qname = (unsigned char *)buffer_write_ptr(&(I(key)->doh.buff), &nbyte);

    int len = change_to_dns_format((char *)qname, I(key)->target_url.hostname); // Mete el host en la posición del qname
    buffer_write_adv(&(I(key)->doh.buff), len);

    qinfo = (struct QUESTION *)buffer_write_ptr(&(I(key)->doh.buff), &nbyte);
    if (type == AF_INET) {
        qinfo->qtype = htons(A);
    } else {
        qinfo->qtype = htons(AAAA);
    }
    qinfo->qclass = htons(1); // Its internet
    buffer_write_adv(&(I(key)->doh.buff), sizeof(struct QUESTION));

    char *aux_buff = (char *)buffer_read_ptr(&(I(key)->doh.buff), &nbyte);
    
    char * write_buffer = malloc(POST_SIZE);
    if (write_buffer == NULL) {
        log(ERROR, "Doing malloc of write buffer")
        return -1;
    }

    int written = create_post((int)nbyte, aux_buff, write_buffer, POST_SIZE); // crea el http request

    if (send(I(key)->doh.server_socket, write_buffer, written, 0) < 0) { // manda el paquete al servidor DOH
        log(ERROR, "Sending DOH request")
        return -1;
    }

    free(write_buffer); // Libera el request http
    return 0;
}


int read_response(struct aibuf * out, int sin_port, int family, int ans_count, struct buffer buff) {

    int sin_family, cant = 0;
    size_t nbytes;
    for (int i = 0; i < ans_count; i++) {
        buffer_read_adv(&buff, get_name(buffer_read_ptr(&buff, &nbytes)) + 1);

        struct R_DATA * data = calloc(1, sizeof(struct R_DATA));
        if (data == NULL) {
            log(ERROR, "Creating data calloc")
            return -1;
        }
        memcpy(data, buffer_read_ptr(&buff, &nbytes), sizeof(struct R_DATA));
        buffer_read_adv(&buff, sizeof(struct R_DATA));

        if (ntohs(data->type) == A) sin_family = AF_INET;
        else if (ntohs(data->type) == AAAA) sin_family = AF_INET6;
        else sin_family = -1;

        if (family == sin_family) {
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
                    memcpy(&out[cant].sa.sin.sin_addr, buffer_read_ptr(&buff, &nbytes), 4);
                    break;
                case AF_INET6:
                    out[cant].sa.sin6.sin6_family = AF_INET6;
                    out[cant].sa.sin6.sin6_port = htons(sin_port);
                    out[cant].sa.sin6.sin6_scope_id = 0;
                    memcpy(&out[cant].sa.sin6.sin6_addr, buffer_read_ptr(&buff, &nbytes), 16);
                    break;
                default:
                    break;
            }
            cant++;
        }
        buffer_read_adv(&buff, ntohs(data->data_len));
        free(data);
    }

    return cant;
}


int resolve_string(struct addrinfo ** addrinfo, const char * target, int port) {
    struct aibuf * out = calloc(1, sizeof(*out) + 1);
    if (out == NULL) {
        log(ERROR, "Doing calloc of out")
        return -1;
    }
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


int change_to_dns_format(char* dns, const char * host) {
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
    return len + 2;
}


int get_name(unsigned char * body) {
    if (*body & 0xC0) { // Chequeo si no esta en formato comprimido
        return 2;
    }
    int cant = 1;
    body++; // para evitar el primer punto
    while (*((char *)body) != 0) {
        body++;
        cant++;
    }

    return cant;
}
