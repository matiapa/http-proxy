#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>
#include <arpa/inet.h>
#include <logger.h>

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

struct method4 {
    unsigned long total_connections;
    unsigned long current_connections;
    unsigned long total_sent;
    unsigned long total_recieved;
};

struct method5 {
    unsigned int max_clients;
    unsigned int timeout;
    unsigned int frequency;
    unsigned char disectors_enabled;
};

enum req_status {
    REQ_SUCCESS = 0,
    REQ_BAD_REQUEST = 1,
    REQ_UNAUTHORIZED = 2
};

union format {
    unsigned short clients;
    short time;
    unsigned char boolean :1;
};

#define BUFFER_SIZE 1024*4
#define MAX_RETRIEVE_METHODS 6
#define MAX_SET_METHODS 4
#define MAX_STRING 50
#define RETRIEVE 0
#define SET 1
#define CURRENT_VERSION 1

char buffer[BUFFER_SIZE];
char pass[32];

char retrieve_methods[MAX_RETRIEVE_METHODS][MAX_STRING] = {"totalConnections", "currentConnections", "totalSend", "totalRecieved", "allStats", "getConfigurations"};
char set_methods[MAX_SET_METHODS][MAX_STRING] = {"setMaxClients", "setClientTimeout", "setStatsFrequency", "setInspection"};

void process_response(struct response_header * res);
void parse_command(char * command, char * response, struct request_header * res, char * buffer);

int main() {
    int sock;
    ssize_t n;
    struct sockaddr_in serverAddr;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        return sock;
    }
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET; // IPv4cle
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(9091);

    if (bind(sock, (const struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        close(sock);
        return -1;
    }

    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(9090);
    dest.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) { // Establece la conexión con el servidor DOH
        return -1;
    }

    // TODO: cambiar esto
    memcpy(pass, "quic", 4);
    pass[4] = 0;

    char command[1024];
    char c;
    int i = 0;
    while(1) {
        i = 0;
        while ((c = getchar()) != '\n') {
            command[i++] = c;
        }
        command[i] = '\0';

        struct request_header req;
        parse_command(command, NULL, &req, buffer);

        if (send(sock, buffer, sizeof(req) + req.length, 0) < 0) { // manda el paquete al servidor DOH
            return -1;
        }

        struct sockaddr_storage clientAddress;
        socklen_t clientAddressSize = sizeof(clientAddress);
        n = 0;
        while (n == 0) {
            n = recvfrom(sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *) &clientAddress, &clientAddressSize);
            if (n < 0) {
                return -1;
            }

            if (n > 0) {
                struct response_header * res = (struct response_header *)buffer;
                if (req.id == res->id) {
                    process_response(res);
                }
            }
        }
    }

    return sock;
}

void process_response(struct response_header * res) {
    if (res->status == REQ_BAD_REQUEST) {
        printf("Bad Request\n");
        return;
    } else if (res->status == REQ_UNAUTHORIZED) {
        printf("%s\n", buffer + sizeof(struct response_header));
    } else {
        if (res->method == 4) {
            struct method4 * results = (struct method4 *)(buffer + sizeof(struct response_header));
            printf("Conecciones: %ld - Conecciones Actuales: %ld - Total Enviados: %ld - Total Recividos: %ld\n", results->total_connections, results->current_connections, results->total_sent, results->total_recieved);
        } else if (res->method == 5) {
            struct method5 * results = (struct method5 *)(buffer + sizeof(struct response_header));
            printf("Clients: %d - Timeout: %d - Frequency: %d - Disector Enabled: %s\n", results->max_clients, results->timeout, results->frequency, results->disectors_enabled == 0 ? "FALSE" : "TRUE");
        } else {
            unsigned long value;
            memcpy(&value, buffer + sizeof(struct response_header), sizeof(long));
            printf("Respuesta: %ld\n", value);
        }
    }
}

void parse_command(char * command, char * response, struct request_header * req, char * buff) {

    for(int i=0; command[i] != 0; i++)
        if(command[i] == '\n'){
            command[i] = 0;
            break;
        }

    if (strncmp(command, "help", 4) == 0) {

        sprintf(response,
                "> Available commands:\n"
                ">> SHOW CONFIG:                          Display current values of config variables\n"
                ">> SHOW STATISTICS:                      Display current values of statistics\n"
                ">> SET variable value:                   Set a config variable value\n"
                ">> HELP:                                 Show this help screen\n\n"
                ">> maxClients:                           Máxima cantidad de clientes posibles\n\n"

                "> Available config variables:\n"
                ">> maxClients:                           Max allowed clients (up to 1000). Default is 1000.\n"
                ">> connectionTimeout:                    Max inactivity time before disconnection, or -1 to disable it. Default is -1.\n"
                ">> statisticsFrequency:                  Frequency of statistics logging, or -1 to disable it.\n"
                ">> disectorsEnabled:                     Whether to extract plain text credentials. Default is 1.\n"
                ">> viaProxyName:                         Host name to use on RFC 7230 required 'Via' header, up to %d characters. Default is proxy hostname.\n"
                ">> clientBlacklist:                      Comma separated list of client IPs to which service must be denied. Max size of list is %d.\n"
                ">> targetBlacklist:                      Comma separated list of target IPs to which connection must be denied. Max size of list is %d.\n"
                ">> logLevel:                             Minimum log level to display, possible values are [DEBUG, INFO, ERROR, FATAL]. Default is DEBUG.\n",

                VIA_PROXY_NAME_SIZE, BLACKLIST_SIZE, BLACKLIST_SIZE
        );

    } else {
        for (int i = 0; i < MAX_RETRIEVE_METHODS; i++) {
            if (strcmp(command, retrieve_methods[i]) == 0) {
                req->version = CURRENT_VERSION;
                req->id = 25; // TODO: cambiarlo por dinamico
                strcpy((char *)req->pass, pass); // TODO: cambiarlo por algo dinámico
                req->type = RETRIEVE;
                req->method = i;
                req->length = 0; // porque es un request del tipo retrieve
                memcpy(buff, req, sizeof(struct request_header));
                return;
            }
        }

        long value;
        char str[MAX_STRING];
        if (sscanf(command, "%s %ld", str, &value) > 0) {
            printf("%ld\n", value);
        }

        for (int i = 0; i < MAX_SET_METHODS; i++) {
            if (strcmp(command, set_methods[i]) == 0) {
                req->version = CURRENT_VERSION;
                req->id = 25; // TODO: cambiarlo por dinamico
                strcpy((char *)req->pass, pass); // TODO: cambiarlo por algo dinámico
                req->type = SET;
                req->method = i;
                req->length = sizeof(long); // porque es un request del tipo retrieve
                memcpy(buff, req, sizeof(struct request_header));
                memcpy(buff + sizeof(struct request_header), &value, sizeof(long));
                return;
            }
        }

    }

//    else if (strncmp(command, "SHOW CONFIG", 11) == 0) {
//
//        char * format =
//                "maxClients: %d\n"
//                "connectionTimeout: %d\n"
//                "statisticsFrequency: %d\n"
//                "disectorsEnabled: %d\n"
//                "viaProxyName: %s\n"
//                "clientBlacklist: %s\n"
//                "targetBlacklist: %s\n"
//                "logLevel: %s\n";
//
//        //sprintf(response, format, PF.maxClients, PF.connectionTimeout, PF.statisticsFrequency, PF.disectorsEnabled, PF.viaProxyName, PF.clientBlacklist, PF.targetBlacklist, levelDescription(PF.logLevel));
//
//    } else if (strncmp(command, "SHOW STATISTICS", 15) == 0) {
//
//        char * format =
//                "current connections: %d\n"
//                "total connections since server restart: %d\n"
//                "total bytes sent: %ld \n"
//                "total bytes recieved: %ld\n";
////        statistics * stats=get_statistics(malloc(sizeof(statistics)));
////        sprintf(response, format,stats->current_connections,stats->total_connections,stats->total_sent,stats->total_recieved );
////        free(stats);
//
//    } else if (strncmp(command, "SET", 3) == 0) {
//
//        set_variable(command + 4, response);
//
//    } else if (command[0] == 0) {
//
//        sprintf(response, "\n");
//
//    } else {
//
//        sprintf(response, "ERROR: Invalid command\n");
//
//    }

}

