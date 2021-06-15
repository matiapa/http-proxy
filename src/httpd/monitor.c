#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <logger.h>
#include <udp_utils.h>
#include <strings.h>
#include <config.h>
#include <monitor.h>
#include <statistics.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define BUFFER_SIZE 1024

#define PASSWORD "QUIC"
#define CURRENT_VERSION 1
#define MASTER_SOCKET_SIZE 2

enum req_status {
    REQ_SUCCESS = 0,
    REQ_BAD_REQUEST = 1,
    REQ_UNAUTHORIZED = 2
};

struct request_header {
    unsigned char version;
    unsigned char pass[32];
    unsigned short id;
    unsigned char type :1;
    unsigned char method :4;
    unsigned char z :3;
    unsigned short length;
};

struct response_header {
    unsigned char version;
    unsigned short id;
    unsigned char status :2;
    unsigned char type :1;
    unsigned char method :4;
    unsigned char z :1;
    unsigned short length;
};

union format {
    unsigned short clients :10;
    short time;
    unsigned char boolean :1;
    unsigned char level :2;
};

struct method5 {
    int timeout;
    int frequency;
    unsigned short max_clients :10;
    unsigned char disectors_enabled :1;
    unsigned char logLevel :2;
};

struct method4 {
    unsigned long total_connections;
    unsigned long current_connections;
    unsigned long total_sent;
    unsigned long total_recieved;
};

int process_request(char * body, struct request_header * request_header, int udp_socket);

void send_retrieve_response(struct request_header * request_header, int body_length, int udp_socket);

void send_success(struct request_header * request_header, int udp_socket);

void send_no_authorization_message(struct request_header * req, int udp_socket);

void send_error(int status, int udp_socket);

void set_variable(char * command, char * response);

Config proxy_conf = {
    .maxClients = 1000,
    .connectionTimeout = -1,
    .statisticsFrequency = 3600,

    .disectorsEnabled = true,

    .viaProxyName = "",
    .clientBlacklist = "",
    .targetBlacklist = "",
    .logLevel = INFO
};

int validate_client(char * pass) {
    return strcmp(pass, PASSWORD) == 0;
}

char req_buffer[BUFFER_SIZE];
char res_buffer[BUFFER_SIZE];

struct sockaddr_storage clientAddress;
socklen_t clientAddressSize = sizeof(clientAddress);
int sockets[MASTER_SOCKET_SIZE];
int master_sockets_size = 0;


void * start_monitor(void * port) {

    bool listen_ipv_both = false;
    if (proxy_conf.proxyArgs.mng_addr == NULL) {
        proxy_conf.proxyArgs.mng_addr = "127.0.0.1";
        listen_ipv_both = true;
    }

    struct addrinfo hint = { .ai_family = PF_UNSPEC, .ai_flags = AI_NUMERICHOST};
    struct addrinfo * listen_addr;
    if (getaddrinfo(proxy_conf.proxyArgs.mng_addr, NULL, &hint, &listen_addr)) {
        log(ERROR, "Invalid manager address");
        exit(EXIT_FAILURE);
    }

    if (listen_addr->ai_family == AF_INET || listen_ipv_both) {
        // Creo el socket para ipv4
        sockets[master_sockets_size++] = create_udp_server(proxy_conf.proxyArgs.mng_addr, port);
        if (sockets[0] == -1) {
            log(FATAL, "Creating server socket for ipv4: %s ", strerror(errno))
        }
        log(DEBUG, "Managment IPv4 socket created: %d ", sockets[0])
    }

    if (listen_addr->ai_family == AF_INET6 || listen_ipv_both) {
        // Creo el socket para ipv6
        sockets[master_sockets_size++] = create_udp6_server(proxy_conf.proxyArgs.mng_addr, port);
        if (sockets[1] == -1) {
            log(FATAL, "Creating server socket for ipv6: %s ", strerror(errno))
        }
        log(DEBUG, "Managment IPv6 socket created: %d ", sockets[1])
    }
    
    fd_set readfds;
    ssize_t n;
    int max_fd;
    while(1) {
        FD_ZERO(&readfds);
        memset(req_buffer, 0, BUFFER_SIZE);
        memset(res_buffer, 0, BUFFER_SIZE);

        max_fd = 0;
        for (int i = 0; i < master_sockets_size; i++) {
            FD_SET(sockets[i], &readfds);
            max_fd = max_fd > sockets[i] ? max_fd : sockets[i];
        }

        int fds = pselect(max_fd + 1, &readfds, NULL, 0, NULL, NULL);
        if (fds == -1) {
            log(ERROR, "Exiting pselect")
            continue;
        }

        for (int i = 0; i < master_sockets_size; i++) {

            if (!FD_ISSET(sockets[i], &readfds)) {
                continue;
            }

            n = recvfrom(sockets[i], req_buffer, BUFFER_SIZE, 0, (struct sockaddr *) &clientAddress, &clientAddressSize);
            if (n < 0) { // si hubo un error
                log(ERROR, "Receiving request from client")
            }

            if (n > 0) { // puede que no haya leido nada y no sea un error
                struct request_header * request_header = calloc(1, sizeof(struct request_header));
                if (request_header == NULL) {
                    log(ERROR, "Doing calloc of request_header")
                    break;
                }

                memcpy(request_header, req_buffer, sizeof(struct request_header)); //-V512

                if (!validate_client((char *)request_header->pass)) {
                    send_no_authorization_message(request_header, sockets[i]);
                } else {
                    int status = process_request(req_buffer + sizeof(struct request_header), request_header, sockets[i]);
                    if (status != REQ_SUCCESS)
                        send_error(status, sockets[i]);
                }
                free(request_header);
            }
        }
    }
}

void send_no_authorization_message(struct request_header * req, int udp_socket) {
    char message[61] = "Acceso denegado\n¿Que protocolo de UDP salió recientemente?";

    struct response_header res_header = {
            .version = CURRENT_VERSION,
            .status = REQ_UNAUTHORIZED,
            .length = strlen(message) + 1,
            .id = req->id,
            .method = req->method,
            .type = req->type,
    };
    memcpy(res_buffer, &res_header, sizeof(struct response_header)); //-V512
    strcpy(res_buffer + sizeof(struct response_header), message);

    if (sendto(udp_socket, res_buffer, sizeof(res_header) + strlen(message) + 1, 0, (const struct sockaddr *) &clientAddress, clientAddressSize) < 0) {
        log(ERROR, "Sending client response")
    }
}

int process_request(char * body, struct request_header * req, int udp_socket) {
    memset(res_buffer, 0, BUFFER_SIZE);
    if (req->version > CURRENT_VERSION) return REQ_BAD_REQUEST;
    if (req->type == 0) {
        struct statistics stats;
        get_statistics(&stats);
        int length = sizeof(long);

        struct method5 method5;
        struct method4 method4;
        switch (req->method) {
            case 0:
                memcpy(res_buffer + sizeof(struct response_header), &stats.total_connections, sizeof(long));
                break;
            case 1:
                memcpy(res_buffer + sizeof(struct response_header), &stats.current_connections, sizeof(int));
                break;
            case 2:
                memcpy(res_buffer + sizeof(struct response_header), &stats.total_sent, sizeof(long));
                break;
            case 3:
                memcpy(res_buffer + sizeof(struct response_header), &stats.total_recieved, sizeof(long));
                break;
            case 4:
                method4.total_connections = stats.total_connections;
                method4.current_connections = stats.current_connections;
                method4.total_sent = stats.total_sent;
                method4.total_recieved = stats.total_recieved;
                length = sizeof(struct method4);
                memcpy(res_buffer + sizeof(struct response_header), &method4, length);
                break;
            case 5:
                method5.max_clients = proxy_conf.maxClients & 0x3FF;
                method5.timeout = proxy_conf.connectionTimeout;
                method5.frequency = proxy_conf.statisticsFrequency;
                method5.disectors_enabled = proxy_conf.disectorsEnabled & 0x1;
                method5.logLevel = proxy_conf.logLevel & 0x3;
                length = sizeof(struct method5);
                memcpy(res_buffer + sizeof(struct response_header), &method5, length);
                break;
            default:
                return REQ_BAD_REQUEST;
        }
        send_retrieve_response(req, length, udp_socket);
    } else {
        if (req->length <= 0)
            return REQ_BAD_REQUEST;

        union format * ft = calloc(1, sizeof(union format));
        if (ft == NULL) return -1;
        
        memcpy(ft, body, sizeof(union format));
        switch (req->method) {
            case 0:
                if (ft->clients > 1000) {
                    free(ft);
                    return REQ_BAD_REQUEST;
                }
                proxy_conf.maxClients = ft->clients;
                break;
            case 1:
                if (req->length < sizeof(ft->time)) {
                    free(ft);
                    return REQ_BAD_REQUEST;
                }
                proxy_conf.connectionTimeout = ft->time;
                break;
            case 2:
                if (req->length < sizeof(ft->time)) {
                    free(ft);
                    return REQ_BAD_REQUEST;
                }
                proxy_conf.statisticsFrequency = ft->time;
                break;
            case 3:
                proxy_conf.disectorsEnabled = ft->boolean;
                break;
            case 4:
                proxy_conf.logLevel = ft->level;
                break;
            default:
                free(ft);
                return REQ_BAD_REQUEST;
        }
        free(ft);
        send_success(req, udp_socket);
    }
    return REQ_SUCCESS;
}

void send_error(int status, int udp_socket) {
    if (status == REQ_BAD_REQUEST) {
        struct response_header res_header = {
                .version = CURRENT_VERSION,
                .status = REQ_BAD_REQUEST,
                .length = 0
        };
        if (sendto(udp_socket, &res_header, sizeof(res_header), 0, (const struct sockaddr *) &clientAddress, clientAddressSize) < 0) {
            log(ERROR, "Sending client response")
        }
    }
}

void send_retrieve_response(struct request_header * request_header, int body_length, int udp_socket) {
    struct response_header * res = (struct response_header *)res_buffer;
    res->version = CURRENT_VERSION;
    res->status = REQ_SUCCESS;
    res->id = request_header->id;
    res->type = request_header->type;
    res->method = request_header->method;
    res->length = body_length;

    if (sendto(udp_socket, res_buffer, sizeof(struct response_header) + body_length, 0, (const struct sockaddr *) &clientAddress, clientAddressSize) < 0) {
        log(ERROR, "Sending client response")
    }
}

void send_success(struct request_header * request_header, int udp_socket) {
    memset(res_buffer, 0, BUFFER_SIZE);
    struct response_header * res = (struct response_header *)res_buffer;
    res->version = CURRENT_VERSION;
    res->status = REQ_SUCCESS;
    res->id = request_header->id;
    res->type = request_header->type;
    res->method = request_header->method;
    res->length = 0;
    if (sendto(udp_socket, res_buffer, sizeof(struct response_header), 0, (const struct sockaddr *) &clientAddress, clientAddressSize) < 0) {
        log(ERROR, "Sending client response")
    }
}













