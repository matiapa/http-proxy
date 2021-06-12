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

#define BUFFER_SIZE 1024
#define PF proxy_conf

#define PASSWORD "quic"
#define CURRENT_VERSION 1

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
    unsigned short length;
};

struct response_header {
    unsigned char version;
    unsigned short id;
    unsigned char status :2;
    unsigned char type :1;
    unsigned char method :4;
    unsigned short length;
};

union format {
    unsigned short clients;
    short time;
    unsigned char boolean :1;
};

int process_request(char * body, struct request_header * request_header);

void send_retrieve_response(struct request_header * request_header, int body_length);

void send_success(struct request_header * request_header);

void send_no_authorization_message();

void send_error(int status);

void set_variable(char * command, char * response);

Config proxy_conf = {
    .maxClients = 1000,
    .connectionTimeout = -1,
    .statisticsFrequency = 3600,

    .disectorsEnabled = true,

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


_Noreturn void * start_monitor(void * port) {

    udp_socket = create_udp_server(port);
    if (udp_socket < 0)
    log(FATAL, "Creating server socket: %s ", strerror(errno))

    ssize_t n;
    while(1) {
        memset(req_buffer, 0, BUFFER_SIZE);
        memset(res_buffer, 0, BUFFER_SIZE);

        n = recvfrom(udp_socket, req_buffer, BUFFER_SIZE, 0, (struct sockaddr *) &clientAddress, &clientAddressSize);
        if (n < 0) { // si hubo un error
            log(ERROR, "Receiving request from client")
        }

        if (n > 0) { // puede que no haya leido nada y no sea un error
            struct request_header *request_header = (struct request_header *) req_buffer;
            if (!validate_client((char *)request_header->pass)) {
                send_no_authorization_message(udp_socket);
            } else {
                int status = process_request(req_buffer + sizeof(struct request_header), request_header);
                if (status != REQ_SUCCESS)
                    send_error(status);
            }
        }
    }
}

void send_no_authorization_message() {
    char message[61] = "Acceso denegado\n¿Que protocolo de UDP salió recientemente?";

    struct response_header res_header = {
            .version = CURRENT_VERSION,
            .status = REQ_UNAUTHORIZED,
            .length = strlen(message) + 1,
    };
    memcpy(res_buffer, &res_header, sizeof(res_header));
    strcpy(res_buffer + sizeof(res_header), message);

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

        union format * ft = (union format *)body;
        switch (req->method) {
            case 0:
                if (ft->clients > 1000)
                    return REQ_BAD_REQUEST;
                proxy_conf.maxClients = ft->clients;
                break;
            case 1:
            case 2:
                proxy_conf.connectionTimeout = ft->time;
                break;
            case 3:
                proxy_conf.disectorsEnabled = ft->boolean;
            default:
                return REQ_BAD_REQUEST;
        }
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

    if (sendto(udp_socket, res_buffer, sizeof(res) + body_length, 0, (const struct sockaddr *) &clientAddress, clientAddressSize) < 0) {
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
    if (sendto(udp_socket, res_buffer, sizeof(res), 0, (const struct sockaddr *) &clientAddress, clientAddressSize) < 0) {
        log(ERROR, "Sending client response")
    }
}

void set_variable(char * command, char * response) {

  char * variable = strtok(command, " ");
  if (variable == NULL) {
    sprintf(response, "ERROR: Missing variable name\n");
    return;
  }

  char * value = strtok(NULL, " ");
  if (value == NULL) {
    sprintf(response, "ERROR: Missing variable value\n");
    return;
  }

  if (strncmp(variable, "maxClients", 10) == 0) {

    int num = atoi(value);
    if (num > 0 && num <= 1000) {
      PF.maxClients = num;
      sprintf(response, "OK: Max clients set to %d\n", num);
    } else {
      sprintf(response, "ERROR: Value must be a natural n such that 0 < n <= 1000\n");
    }

  } else if (strncmp(variable, "connectionTimeout", 17) == 0) {

    int num = atoi(value);
    if (num > 0 || num == -1) {
      PF.connectionTimeout = num;
      sprintf(response, "OK: Connection timeout set to %d\n", num);
    } else {
      sprintf(response, "ERROR: Value must be a natural n such that 0 < n or n = -1\n");
    }

  } else if (strncmp(variable, "statisticsFrequency", 19) == 0) {

    int num = atoi(value);
    if (num == 0 || num == 1) {
      PF.statisticsFrequency = num;
      sprintf(response, "OK: Statistics %s\n", num ? "enabled" : "disabled");
    } else {
      sprintf(response, "ERROR: Value must be a natural n such that n = 0 or n = 1\n");
    }

  } else if (strncmp(variable, "disectorsEnabled", 16) == 0) {

    int num = atoi(value);
    if (num == 0 || num == 1) {
      PF.disectorsEnabled = num;
      sprintf(response, "OK: Disectors %s\n", num ? "enabled" : "disabled");
    } else {
      sprintf(response, "ERROR: Value must be a natural n such that n = 0 or n = 1\n");
    }

  } else if (strncmp(variable, "viaProxyName", 12) == 0) {

    strncpy(PF.viaProxyName, value, VIA_PROXY_NAME_SIZE);

    sprintf(response, "OK: Proxy name set to %s\n", PF.viaProxyName);

  } else if (strncmp(variable, "clientBlacklist", 15) == 0) {

    strncpy(PF.clientBlacklist, value, BLACKLIST_SIZE);

    sprintf(response, "OK: Client black list set to %s\n", PF.clientBlacklist);

  } else if (strncmp(variable, "targetBlacklist", 15) == 0) {

    strncpy(PF.targetBlacklist, value, BLACKLIST_SIZE);

    sprintf(response, "OK: Target black list set to %s\n", PF.targetBlacklist);

  } else if (strncmp(variable, "logLevel", 8) == 0) {

    int level = descriptionLevel(value);
    if (level >= 0) {
      PF.logLevel = (LOG_LEVEL) level;
      sprintf(response, "OK: Log level set to %s\n", levelDescription(level));
    } else {
      sprintf(response, "ERROR: Value must be one of [DEBUG, INFO, ERROR, FATAL]\n");
    }

  }

}















