#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <strings.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <client_args.h>
#include <sys/termios.h>

union format {
    unsigned short clients :10;
    int time;
    unsigned char boolean :1;
    unsigned char level :2;
};

struct request_header {
    unsigned char version;
    unsigned char pass[32];
    unsigned short id;
    unsigned char type :1;
    unsigned char method :4;
    unsigned char z :3;
    unsigned short length;
    union format ft;
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

struct method4 {
    unsigned long total_connections;
    unsigned long current_connections;
    unsigned long total_sent;
    unsigned long total_recieved;
};

struct method5 {
    int timeout;
    int frequency;
    unsigned short max_clients :10;
    unsigned char disectors_enabled :1;
    unsigned char logLevel :2;
};

enum req_status {
    REQ_SUCCESS = 0,
    REQ_BAD_REQUEST = 1,
    REQ_UNAUTHORIZED = 2
};

#define BUFFER_SIZE 1024
#define MAX_RETRIEVE_METHODS 6
#define MAX_SET_METHODS 7
#define MAX_CLIENT_METHODS 2
#define MAX_STRING 20
#define RETRIEVE 0
#define SET 1
#define CURRENT_VERSION 1
#define COMMAND_SIZE 40

char buffer[BUFFER_SIZE];
char pass[32];
char logLevels[4][6] = {"DEBUG", "INFO", "ERROR", "FATAL"};

char retrieve_methods[MAX_RETRIEVE_METHODS][MAX_STRING] = {"totalConnections", "currentConnections", "totalSend", "totalRecieved", "allStats", "getConfigurations"};
char set_methods[MAX_SET_METHODS][MAX_STRING] = {"setMaxClients", "setClientTimeout", "setStatsFrequency", "setDisector", "setLoggingLevel"};
char client_methods[MAX_CLIENT_METHODS][MAX_STRING] = {"help", "changePassword"};

void process_response(struct response_header * res);
int parse_command(char * command, struct request_header * res, char * buffer);
void print_error(char * message);
int is_number(const char * str);
void print_help();
void get_password();

void red() {
    printf("\033[1;31m");
}

void green() {
    printf("\033[0;32m");
}

void cyan() {
    printf("\033[0;36m");
}

void reset() {
    printf("\033[0m");
}

int main(int argc, char **argv) {

    struct client_args args;
    client_parse_args(argc, argv, &args);

    int sock;
    ssize_t n;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        return sock;
    }

    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(args.monitor_port);
    dest.sin_addr.s_addr = inet_addr(args.monitor_addr);

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        return -1;
    }

    get_password();

    char command[COMMAND_SIZE];
    int c, flag, i;
    while(1) {
        flag = i = 0;
        green();
        printf("> ");
        reset();
        while ((c = getchar()) != EOF && c != '\n' && !flag) {
            if (i == COMMAND_SIZE)
                flag = 1;

            command[i++] = (char)c;
        }
        command[i] = 0;
        if (flag) {
            red();
            printf("\nComando Invalido\n");
            reset();
            continue;
        }

        memset(buffer, 0, BUFFER_SIZE);
        struct request_header req;
        if (parse_command(command, &req, buffer) <= 0) continue; // envio mal el command o fue un cambio del cliente

        if (send(sock, buffer, sizeof(req) + req.length, 0) < 0) {
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
        red();
        printf("Pedido Invalido\n");
        reset();
        return;
    } else if (res->status == REQ_UNAUTHORIZED) {
        printf("%s\n", buffer + sizeof(struct response_header));
    } else {
        if (res->type == 0){
            if (res->method == 4) {
                struct method4 * results = (struct method4 *)(buffer + sizeof(struct response_header));
                cyan();
                printf("- Conexiones Históricas: ");
                reset();
                printf("%lu\n", results->total_connections);
                cyan();
                printf("- Conexiones Actuales: ");
                reset();
                printf("%lu\n", results->current_connections);
                cyan();
                printf("- Total Enviados: ");
                reset();
                printf("%lu\n", results->total_sent);
                cyan();
                printf("- Total Recibidos: ");
                reset();
                printf("%lu\n", results->total_recieved);
            } else if (res->method == 5) {
                struct method5 * results = (struct method5 *)(buffer + sizeof(struct response_header));
                cyan();
                printf("- Clientes Máximos: ");
                reset();
                printf("%d\n", results->max_clients);
                cyan();
                printf("- Timeout: ");
                reset();
                printf("%d\n", results->timeout);
                cyan();
                printf("- Frecuencia de estadística: ");
                reset();
                printf("%d\n", results->frequency);
                cyan();
                printf("- Disectores habilitados: ");
                reset();
                printf("%s\n", results->disectors_enabled == 0 ? "FALSE" : "TRUE");
                cyan();
                printf("- Log Level: ");
                reset();
                printf("%s\n", logLevels[results->logLevel]);
            } else {
                long value;
                memcpy(&value, buffer + sizeof(struct response_header), sizeof(long));
                cyan();
                printf("Respuesta: ");
                reset();
                printf("%ld\n", value);
            }
        } else {
            if (res->status == REQ_SUCCESS) {
                green();
                printf("Valor Actualizado\n");
                reset();
            }

        }
    }
}

int parse_command(char * command, struct request_header * req, char * buff) {

    for (int i = 0; i < MAX_CLIENT_METHODS; i++) {
        if (strcmp(command, client_methods[i]) == 0) {
            if (i == 0) {
                print_help();
            } else {
                get_password();
            }
            return 0;
        }
    }

    for (int i = 0; i < MAX_RETRIEVE_METHODS; i++) {
        if (strcmp(command, retrieve_methods[i]) == 0) {
            req->version = CURRENT_VERSION;
            req->id = 0;
            strcpy((char *)req->pass, pass);
            req->type = RETRIEVE;
            req->method = i;
            req->length = 0; // porque es un request del tipo retrieve
            memcpy(buff, req, sizeof(struct request_header));
            return 1;
        }
    }

    int value;

    char * token = strtok(command, " ");
    if (token == NULL)
        return -1;
        
    char * num = strtok(NULL, " ");


    if (num != NULL && is_number(num)) {
        sscanf(num, "%d", &value);
    } else {
        red();
        printf("Comando invalido\n");
        reset();
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
                    req->ft.clients = value & 0x3FF;
                    break;
                case 1:
                case 2:
                    req->ft.time = value;
                    break;
                case 3:
                    if (value > 1) {
                        print_error("El valor no puede ser mayor a 1");
                        return -1;
                    }
                    req->ft.boolean = value;
                    break;
                case 4:
                    if (value > 3) {
                        print_error("El valor no puede ser mayor a 3");
                        return -1;
                    }
                    req->ft.level = value;
                default:
                    break;
            }
            req->version = CURRENT_VERSION;
            req->id = 0;
            strcpy((char *)req->pass, pass);
            req->type = SET;
            req->method = i;
            req->length = sizeof(req->ft); // porque es un request del tipo retrieve
            memcpy(buff, req, sizeof(struct request_header));

            return 1;
        }
    }

    return -1;

}

void print_error(char * message) {
    red();
    printf("Error: %s\n", message);
    reset();
}

int is_number(const char * str) {
    int i = 0;
    if (*str == '-') {
        str++;
    }
    while(*str != '\0') {
        if (*str > '9' || *str < '0') return 0;
        str++;
        i++;
    }
    return i > 0 ? 1 : 0;
}

void print_help() {
    printf("\033[0;32mComandos Disponibles:\033[0m\n\n"
           "\033[0;36m> changePassword \033[0m              permite cambiar la contraseña con la que se\n"
           "                               accede al managment.\n\n"
           "\033[0;36m> totalConnections \033[0m            retorna la cantidad historica de clientes que\n"
           "                               se conectaron al proxy.\n\n"
           "\033[0;36m> currentConnections \033[0m          retorna la cantidad actual de clientes que estan\n"
           "                               conectados al proxy.\n\n"
           "\033[0;36m> totalSend \033[0m                   retorna la cantidad de bytes que han sido\n"
           "                               enviados a traves dle proxy.\n\n"
           "\033[0;36m> totalRecieved  \033[0m              retorna la cantidad de bytes que han sido\n"
           "                               recividas por el proxy.\n\n"
           "\033[0;36m> allStats    \033[0m                 retorna todos los valores estadísticos.\n\n"
           "\033[0;36m> getConfigurations \033[0m           retorna la configuración actual del proxy.\n\n"
           "\033[0;36m> setMaxClients <valor> \033[0m       recive un valor númerico menor a 1000 utilizado\n"
           "                               para configurar la máxima cantidad de clientes\n"
           "                               concurrentes que puede tener el proxy.\n\n"
           "\033[0;36m> setClientTimeout <valor>  \033[0m   recive un valor númerico que representa tiempo\n"
           "                               en segundos utilizado para configurar el tiempo\n"
           "                               el cual un cliente puede estar inactivo.\n\n"
           "\033[0;36m> setStatsFrequency <valor> \033[0m   recive un valor númerico que representa tiempo\n"
           "                               en segundos utilizado para configurar cada cuanto\n"
           "                               se guardan los valores estadísticos del programa.\n\n"
           "\033[0;36m> setDisector <valor>  \033[0m        recive un valor del conjunto {0, 1}, 1 siendo\n"
           "                               activar y 0 desactivar, el cual se utiliza para\n"
           "                               habilitar/deshabilitar la inspección de credenciales.\n\n"
           "\033[0;36m> setLoggingLevel <valor> \033[0m     recive un valor númerico cuyo valor puede ser [0, 1, 2, 3],\n"
           "                               donde DEBUG = 0, INFO = 1, ERROR = 2, FATAL = 3\n\n"
           "\033[0;32mConsultar el RFC 20216 para más información\033[0m\n");
}

// https://stackoverflow.com/questions/6856635/hide-password-input-on-terminal
int getch() {
    struct termios oldtc, newtc;
    int ch;
    tcgetattr(STDIN_FILENO, &oldtc);
    newtc = oldtc;
    newtc.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newtc);
    ch=getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldtc);
    return ch;
}

void get_password() {
    int c;
    memset(pass, 0, sizeof(pass));
    int i = 0;
    while (i == 0) {
        cyan();
        printf("Ingresar Contraseña: ");
        reset();
        while ((c = getch()) != EOF && c != '\n') {
            pass[i++] = (char)c;
        }
    }
    putchar('\n');
    pass[i] = '\0';
}


