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
#define PF proxy_conf

#define PASSWORD "quic"
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
    unsigned int clients;
    short time;
    unsigned char boolean :1;
    unsigned char level :2;
};

int process_request(char * body, struct request_header * request_header);

void send_retrieve_response(struct request_header * request_header, int body_length);

void send_success(struct request_header * request_header);

void send_no_authorization_message(struct request_header * req);

void send_error(int status);

void set_variable(char * command, char * response);

Config proxy_conf = {
    .maxClients = 1000,
    .connectionTimeout = -1,
    .statisticsFrequency = 3600,

    .disectorsEnabled = false,

    .viaProxyName = "",
    .clientBlacklist = "",
    .targetBlacklist = "",
    .logLevel = DEBUG
};

int validate_client(char * pass) {
    return strcmp(pass, PASSWORD) == 0;
}

char req_buffer[BUFFER_SIZE];
char res_buffer[BUFFER_SIZE];

struct sockaddr_storage clientAddress;
socklen_t clientAddressSize = sizeof(clientAddress);
int udp_socket;
int udp6_socket;
int sockets[MASTER_SOCKET_SIZE];


void * start_monitor(void * port) {

    // Creo el socket para ipv4
    sockets[0] = create_udp_server(port);
    if (sockets[0] == -1) {
        log(FATAL, "Creating server socket for ipv4: %s ", strerror(errno))
    }
    log(INFO, "Server socket for ipv4 created: %d ", sockets[0])

    // Creo el socket para ipv6
    sockets[1] = create_udp6_server(port);
    if (sockets[1] == -1) {
        log(FATAL, "Creating server socket for ipv6: %s ", strerror(errno))
    }
    log(INFO, "Server socket for ipv6 created: %d ", sockets[1])


    fd_set readfds;
    ssize_t n;
    int max_fd;
    while(1) {
        FD_ZERO(&readfds);
        memset(req_buffer, 0, BUFFER_SIZE);
        memset(res_buffer, 0, BUFFER_SIZE);

        max_fd = 0;
        for (int i = 0; i < MASTER_SOCKET_SIZE; i++) {
            FD_SET(sockets[i], &readfds);
            max_fd = max_fd > sockets[i] ? max_fd : sockets[i];
        }

        int fds = pselect(max_fd + 1, &readfds, NULL, 0, NULL, NULL);
        if (fds == -1) {
            log(ERROR, "Exiting pselect")
            continue;
        }

        for (int i = 0; i < MASTER_SOCKET_SIZE; i++) {

            if (!FD_ISSET(sockets[i], &readfds)) {
                continue;
            }

            n = recvfrom(udp_socket, req_buffer, BUFFER_SIZE, 0, (struct sockaddr *) &clientAddress, &clientAddressSize);
            if (n < 0) { // si hubo un error
                log(ERROR, "Receiving request from client")
            }

            if (n > 0) { // puede que no haya leido nada y no sea un error
                struct request_header * request_header = calloc(1, sizeof(struct request_header));
                if (request_header == NULL) {
                    log(ERROR, "Doing calloc of request_header")
                    return -1;
                }

                memcpy(request_header, req_buffer, sizeof(struct request_header)); //-V512

                if (!validate_client((char *)request_header->pass)) {
                    send_no_authorization_message(request_header);
                } else {
                    int status = process_request(req_buffer + sizeof(struct request_header), request_header);
                    if (status != REQ_SUCCESS)
                        send_error(status);
                }
                free(request_header);
            }
        }
    }
}

void send_no_authorization_message(struct request_header * req) {
    char message[61] = "Acceso denegado\n¿Que protocolo de UDP salió recientemente?";

    struct response_header res_header = {
            .version = CURRENT_VERSION,
            .status = REQ_UNAUTHORIZED,
            .length = strlen(message) + 1,
            .id = req->id,
            .method = req->method,
            .type = req->type,
    };
    memcpy(res_buffer, &res_header, sizeof(struct response_header));
    strcpy(res_buffer + sizeof(struct response_header), message);

    if (sendto(udp_socket, res_buffer, sizeof(res_header) + strlen(message) + 1, 0, (const struct sockaddr *) &clientAddress, clientAddressSize) < 0) {
        log(ERROR, "Sending client response")
    }
}

int process_request(char * body, struct request_header * req) {
    memset(res_buffer, 0, BUFFER_SIZE);
    if (req->version > CURRENT_VERSION) return REQ_BAD_REQUEST;
    if (req->type == 0) {
        struct statistics stats;
        get_statistics(&stats);
        int length = sizeof(long);
        switch (req->method) {
            case 0:
                memcpy(res_buffer + sizeof(struct response_header), &stats.total_connections, sizeof(long));
                break;
            case 1:
                memcpy(res_buffer + sizeof(struct response_header), &stats.current_connections, sizeof(long));
                break;
            case 2:
                memcpy(res_buffer + sizeof(struct response_header), &stats.total_sent, sizeof(long));
                break;
            case 3:
                memcpy(res_buffer + sizeof(struct response_header), &stats.total_recieved, sizeof(long));
                break;
            case 4:
                memcpy(res_buffer + sizeof(struct response_header), &stats.total_connections, sizeof(long));
                memcpy(res_buffer + sizeof(struct response_header) + sizeof(long), &stats.current_connections, sizeof(long));
                memcpy(res_buffer + sizeof(struct response_header) + sizeof(long)*2, &stats.total_sent, sizeof(long));
                memcpy(res_buffer + sizeof(struct response_header) + sizeof(long)*3, &stats.total_recieved, sizeof(long));
                length *= 4;
                break;
            case 5:
                memcpy(res_buffer + sizeof(struct response_header), &proxy_conf.maxClients, sizeof(int));
                memcpy(res_buffer + sizeof(struct response_header) + sizeof(int), &proxy_conf.connectionTimeout, sizeof(int));
                memcpy(res_buffer + sizeof(struct response_header) + sizeof(int)*2, &proxy_conf.statisticsFrequency, sizeof(int));
                memcpy(res_buffer + sizeof(struct response_header) + sizeof(int)*3, &proxy_conf.disectorsEnabled, sizeof(char));
                length = sizeof(int) * 3 + sizeof(char);
                break;
            default:
                return REQ_BAD_REQUEST;
        }
        send_retrieve_response(req, length);
    } else {
        if (req->length <= 0)
            return REQ_BAD_REQUEST;

        union format * ft = calloc(1, sizeof(union format));
        memcpy(ft, body, sizeof(union format));
        switch (req->method) {
            case 0:
                if (ft->clients > 1000)
                    return REQ_BAD_REQUEST;
                proxy_conf.maxClients = ft->clients;
                break;
            case 1:
                proxy_conf.connectionTimeout = ft->time;
                break;
            case 2:
                proxy_conf.statisticsFrequency = ft->time;
                break;
            case 3:
                proxy_conf.disectorsEnabled = ft->boolean;
                break;
            case 4:
                proxy_conf.logLevel = ft->level;
                break;
            default:
                return REQ_BAD_REQUEST;
        }
        free(ft);
        send_success(req);
    }
    return REQ_SUCCESS;
}

void send_error(int status) {
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

void send_retrieve_response(struct request_header * request_header, int body_length) {
    struct response_header * res = (struct response_header *)res_buffer;
    res->version = CURRENT_VERSION;
    res->status = SUCCESS;
    res->id = request_header->id;
    res->type = request_header->type;
    res->method = request_header->method;
    res->length = body_length;

    if (sendto(udp_socket, res_buffer, sizeof(struct response_header) + body_length, 0, (const struct sockaddr *) &clientAddress, clientAddressSize) < 0) {
        log(ERROR, "Sending client response")
    }
}

void send_success(struct request_header * request_header) {
    memset(res_buffer, 0, BUFFER_SIZE);
    struct response_header * res = (struct response_header *)res_buffer;
    res->version = CURRENT_VERSION;
    res->status = SUCCESS;
    res->id = request_header->id;
    res->type = request_header->type;
    res->method = request_header->method;
    res->length = 0;
    if (sendto(udp_socket, res_buffer, sizeof(struct response_header), 0, (const struct sockaddr *) &clientAddress, clientAddressSize) < 0) {
        log(ERROR, "Sending client response")
    }
}













