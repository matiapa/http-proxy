#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>
#include <arpa/inet.h>
#include <logger.h>
#include <stdio.h>

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

union format {
    unsigned short clients;
    short time;
    unsigned char boolean :1;
    unsigned char level :2;
};

enum req_status {
    REQ_SUCCESS = 0,
    REQ_BAD_REQUEST = 1,
    REQ_UNAUTHORIZED = 2
};

#define BUFFER_SIZE 1024*4
#define MAX_RETRIEVE_METHODS 6
#define MAX_SET_METHODS 7
#define MAX_CLIENT_METHODS 2
#define MAX_STRING 20
#define RETRIEVE 0
#define SET 1
#define CURRENT_VERSION 1

char buffer[BUFFER_SIZE];
char pass[32];

char retrieve_methods[MAX_RETRIEVE_METHODS][MAX_STRING] = {"totalConnections", "currentConnections", "totalSend", "totalRecieved", "allStats", "getConfigurations"};
char set_methods[MAX_SET_METHODS][MAX_STRING] = {"setMaxClients", "setClientTimeout", "setStatsFrequency", "setDisector", "setLoggingLevel", "clientBlacklist", "targetBlacklist"};
char client_methods[MAX_CLIENT_METHODS][MAX_STRING] = {"help", "changePassword"};

void process_response(struct response_header * res);
int parse_command(char * command, struct request_header * res, char * buffer);
void print_error(char * message);
int is_number(const char * str);
void print_help();
void get_password();

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

    get_password();

    char command[1024];
    while(1) {

        scanf("%s", command);

        struct request_header req;
        if (parse_command(command, &req, buffer) <= 0) continue; // envio mal el command o fue un cambio del cliente

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

}

void process_response(struct response_header * res) {
    if (res->status == REQ_BAD_REQUEST) {
        printf("Bad Request\n");
        return;
    } else if (res->status == REQ_UNAUTHORIZED) {
        printf("%s\n", buffer + sizeof(struct response_header));
    } else {
        if (res->type == 0){
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
        } else {
            if (res->status == REQ_SUCCESS)
                printf("Valor Actualizado\n");
        }
    }
}

int parse_command(char * command, struct request_header * req, char * buff) {

    for (int i = 0; i < MAX_CLIENT_METHODS; i++) {
        if (strcmp(command, client_methods[i]) == 0) {
            if (i == 0) {
                print_help();
            } else if (i == 1) {
                get_password();
            }
            return 0;
        }
    }

    for (int i = 0; i < MAX_RETRIEVE_METHODS; i++) {
        if (strcmp(command, retrieve_methods[i]) == 0) {
            req->version = CURRENT_VERSION;
            req->id = 25; // TODO: cambiarlo por dinamico
            strcpy((char *)req->pass, pass); // TODO: cambiarlo por algo dinámico
            req->type = RETRIEVE;
            req->method = i;
            req->length = 0; // porque es un request del tipo retrieve
            memcpy(buff, req, sizeof(struct request_header));
            return 1;
        }
    }

    int value;

    char * token = strtok(command, " ");
    char * num = strtok(NULL, " ");


    if (num != NULL && is_number(num)) {
        sscanf(num, "%d", &value);
    } else {
        printf("Comando invalido\n");
        return -1;
    }

    for (int i = 0; i < MAX_SET_METHODS; i++) {
        if (strcmp(token, set_methods[i]) == 0) {

            switch (i) {
                case 0:
                    if (value > 1000) {
                        print_error("El valor no puede ser mayor a 1000");
                        return -1;
                    }
                    memcpy(buff + sizeof(struct request_header), &value, sizeof(int));
                    break;
                case 1:
                case 2:
                    memcpy(buff + sizeof(struct request_header), &value, sizeof(int));
                    break;
                case 3:
                    if (value > 1) {
                        print_error("El valor no puede ser mayor a 1");
                        return -1;
                    }
                    memcpy(buff + sizeof(struct request_header), &value, sizeof(char));
                    break;
                default:
                    break;
            }
            req->version = CURRENT_VERSION;
            req->id = 25; // TODO: cambiarlo por dinamico
            strcpy((char *)req->pass, pass); // TODO: cambiarlo por algo dinámico
            req->type = SET;
            req->method = i;
            req->length = sizeof(long); // porque es un request del tipo retrieve
            memcpy(buff, req, sizeof(struct request_header));

            return 1;
        }
    }

    return -1;

}

void print_error(char * message) {
    printf("Error: %s\n", message);
}

int is_number(const char * str) {
    int i = 0;
    while(*str != '\0') {
        if (*str > '9' || *str < '0') return 0;
        str++;
        i++;
    }
    return i > 0 ? 1 : 0;
}

void print_help() {
    printf("> Comandos Disponibles:\n"
           ">> changePassword               permite cambiar la contraseña con la que se\n"
           "                                accede al managment.\n\n"
           ">> totalConnections             retorna la cantidad historica de clientes que\n"
           "                                se conectaron al proxy.\n\n"
           ">> currentConnections           retorna la cantidad actual de clientes que estan\n"
           "                                conectados al proxy.\n\n"
           ">> totalSend                    retorna la cantidad de bytes que han sido\n"
           "                                enviados a traves dle proxy.\n\n"
           ">> totalRecieved                retorna la cantidad de bytes que han sido\n"
           "                                recividas por el proxy.\n\n"
           ">> allStats                     retorna todos los valores estadísticos.\n\n"
           ">> getConfigurations            retorna la configuración actual del proxy.\n\n"
           ">> setMaxClients <valor>        recive un valor númerico menor a 1000 utilizado\n"
           "                                para configurar la máxima cantidad de clientes\n"
           "                                concurrentes que puede tener el proxy.\n\n"
           ">> setClientTimeout <valor>     recive un valor númerico que representa tiempo\n"
           "                                en segundos utilizado para configurar el tiempo\n"
           "                                el cual un cliente puede estar inactivo.\n\n"
           ">> setStatsFrequency <valor>    recive un valor númerico que representa tiempo\n"
           "                                en segundos utilizado para configurar cada cuanto\n"
           "                                se guardan los valores estadísticos del programa.\n\n"
           ">> setDisector <valor>          recive un valor del conjunto {0, 1}, 1 siendo\n"
           "                                activar y 0 desactivar, el cual se utiliza para\n"
           "                                habilitar/deshabilitar la inspección de credenciales.\n\n"
           ">> setLoggingLevel <valor>      recive un valor númerico cuyo valor puede ser [0, 1, 2, 3],\n"
           "                                donde DEBUG = 0, INFO = 1, ERROR = 2, FATAL = 3\n\n"
           "> Consultar el RFC del Protocolo para más información\n");
}

void get_password() {
    char c;
    memset(pass, 0, sizeof(pass));
    int i = 0;
    while (i == 0) {
        printf("Ingresar Contraseña: ");
        while ((c = getchar()) != EOF && c != '\n') {
            pass[i++] = c;
        }
    }
    pass[i] = '\0';
}
