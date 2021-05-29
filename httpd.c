#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include "logger.h"
#include "util.h"

#define MAXPENDING 5
#define BUFFSIZE 256
#define MAX_ADDR_BUFFER 128
#define MAX_SOCKETS 30
#define TRUE 1
#define MAX_PENDING_CONNECTIONS   3

struct buffer {
    char *buffer;
    size_t len;     // longitud del buffer
    size_t from;    // desde donde falta escribir
};

static char addrBuffer[MAX_ADDR_BUFFER]; // se usa para imprimir

int setupTCPServerSocket(int servPort, const char * listenIp);
int acceptTCPConnection(int servSock);
int handleTCPEchoClient(int clntSocket, int otherSocket);
void handleWrite(int socket, struct buffer *buffer, fd_set *writefds);
void clear(struct buffer *buffer);

int main(int argc, char *argv[]) {
    if (argc != 4) {
        log(FATAL, "usage: %s <Server Port>", argv[0]);
    }

    char * servPort = argv[1];
    char * listenPort = argv[2];
    char * listenIp = argv[3];

    int client_socket[2][MAX_SOCKETS];
    int max_clients[2] = {MAX_SOCKETS, MAX_SOCKETS};
    int master_socket[2];
    int master_socket_size = 0;
    int ports[2] = {9090, 9091};
    int sd, max_sd, activity, new_socket, addrlen;
    long valread;
    char buffer[BUFFSIZE + 1];
    struct sockaddr_in address;
    fd_set readfds;
    fd_set writefds;

    // Agregamos un buffer de escritura asociado a cada socket, para no bloquear por escritura
    struct buffer bufferWrite[MAX_SOCKETS];
    memset(bufferWrite, 0, sizeof bufferWrite);

    int listenSock = setupTCPServerSocket(ports[0], listenIp);
    if (listenSock < 0)
        return 1;
    master_socket[0] = listenSock;
    master_socket_size++;

    int reciverSock = setupTCPServerSocket(ports[1], listenIp);
    if (reciverSock < 0)
        return 1;
    master_socket[1] = reciverSock;
    master_socket_size++;

    memset(client_socket, 0, sizeof(client_socket));

    FD_ZERO(&writefds);
    while(1) {
        log(DEBUG, "Started While...");
        FD_ZERO(&readfds);

        for (int sdMaster = 0; sdMaster < master_socket_size; sdMaster++) {
            FD_SET(master_socket[sdMaster], &readfds);
            if (FD_ISSET(master_socket[sdMaster], &readfds)) {
                log(DEBUG, "Esta seteado...");
            }
        }

        for (int sdMaster = 0; sdMaster < master_socket_size; sdMaster++) {
            for (int i = 0; i < max_clients[sdMaster]; i++) {
                // socket descriptor
                sd = client_socket[sdMaster][i];

                // if valid socket descriptor then add to read list
                if (sd > 0)
                    FD_SET(sd, &readfds);

                // highest file descriptor number, need it for the select function
                if (sd > max_sd)
                    max_sd = sd;
            }
        }
        log(DEBUG, "Started Select...");

        activity = select(max_sd + 1, &readfds, &writefds, NULL, NULL);
        log(DEBUG, "select has something...");

        if ((activity < 0) && (errno != EINTR)) {
            log(ERROR, "select error, errno=%d", errno);
            continue;
        }

        for (int sdMaster = 0; sdMaster < master_socket_size; sdMaster++) {
            int mSock = master_socket[sdMaster];
            if (FD_ISSET(mSock, &readfds)) {
                if ((new_socket = acceptTCPConnection(mSock)) < 0) {
                    log(ERROR, "Accept error on master socket %d", mSock);
                    continue;
                }

                // add new socket to array of sockets
                for (int i = 0; i < max_clients[sdMaster]; i++) {
                    // if position is empty
                    if (client_socket[sdMaster][i] == 0) {
                        client_socket[sdMaster][i] = new_socket;
                        log(DEBUG, "Adding to list of sockets as %d\n", i);
                        break;
                    }
                }
            }
        }

        for (int sdMaster = 0; sdMaster < master_socket_size; sdMaster++) {
            for (int i = 0; i < max_clients[sdMaster]; i++) {
                sd = client_socket[sdMaster][i];

                if (FD_ISSET(sd, &writefds)) {
                    if (sdMaster == 0) {
                        handleWrite(client_socket[1][0], bufferWrite + i, &writefds);
                    } else {
                        handleWrite(client_socket[0][0], bufferWrite + i, &writefds);
                    }
                }
            }
        }

        for (int sdMaster = 0; sdMaster < master_socket_size; sdMaster++) {
            for (int i = 0; i < max_clients[sdMaster]; i++) {
                sd = client_socket[sdMaster][i];

                if (FD_ISSET(sd, &readfds)) {
                    //Check if it was for closing , and also read the incoming message
                    if ((valread = read(sd, buffer, BUFFSIZE)) <= 0) {
                        //Somebody disconnected , get his details and print
                        getpeername(sd, (struct sockaddr *) &address, (socklen_t *) &addrlen);
                        log(INFO, "Host disconnected , ip %s , port %d \n", inet_ntoa(address.sin_addr),
                            ntohs(address.sin_port));

                        //Close the socket and mark as 0 in list for reuse
                        close(sd);
                        client_socket[sdMaster][i] = 0;

                        FD_CLR(sd, &writefds);
                        // Limpiamos el buffer asociado, para que no lo "herede" otra sesión
                        clear(bufferWrite + i);
                    } else {
                        log(DEBUG, "Received %zu bytes from socket %d\n", valread, sd);
                        // activamos el socket para escritura y almacenamos en el buffer de salida
                        FD_SET(sd, &writefds);

                        // Tal vez ya habia datos en el buffer
                        // TODO: validar realloc != NULL
                        bufferWrite[i].buffer = realloc(bufferWrite[i].buffer, bufferWrite[i].len + valread);
                        memcpy(bufferWrite[i].buffer + bufferWrite[i].len, buffer, valread);
                        bufferWrite[i].len += valread;
                    }
                }
            }
        }

        return 0;
    }
}

int setupTCPServerSocket(int servPort, const char * listenIp) {
    struct sockaddr_in address;
    int opt = TRUE;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(servPort);

    int servSocket;

    if ((servSocket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        log(ERROR, "socket IPv4 failed");
    } else {
        //set master socket to allow multiple connections , this is just a good habit, it will work without this
        if (setsockopt(servSocket, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt)) < 0) {
            log(ERROR, "set IPv4 socket options failed");
        }

        // bind the socket to localhost port 8888
        if (bind(servSocket, (struct sockaddr *) &address, sizeof(address)) < 0) {
            log(ERROR, "bind for IPv4 failed");
            close(servSocket);
            return -1;
        } else {
            if (listen(servSocket, MAX_PENDING_CONNECTIONS) < 0) {
                log(ERROR, "listen on IPv4 socket failes");
                close(servSocket);
                return -1;
            } else {
                log(DEBUG, "Waiting for TCP IPv4 connections on socket %d\n", servSocket);
                return servSocket;
            }
        }
    }

    return servSocket;
}

int acceptTCPConnection(int servSock) {
    struct sockaddr_storage clntAddr;
    socklen_t clntAddrLen = sizeof(clntAddr);

    int clntSock = accept(servSock, (struct sockaddr *) &clntAddr, &clntAddrLen);
    if (clntSock < 0) {
        log(ERROR, "accept() failed");
        return -1;
    }

    printSocketAddress((struct sockaddr *) &clntAddr, addrBuffer);
    log(INFO, "Handling client %s", addrBuffer);

    return clntSock;
}

int handleTCPEchoClient(int clntSocket, int otherSocket) {
    char buffer[BUFFSIZE];
    ssize_t numBytesRcvd = recv(clntSocket, buffer, BUFFSIZE, 0);
    if (numBytesRcvd < 0) {
        log(ERROR, "recv() failed");
        return -1;   // TODO definir codigos de error
    }

    while (numBytesRcvd > 0) { // 0 indicates end of stream, termina cuando recv devuelve 0
        // Echo message back to client
        ssize_t numBytesSent = send(otherSocket, buffer, numBytesRcvd, 0);
        if (numBytesSent < 0) {
            log(ERROR, "send() failed");
            return -1;   // TODO definir codigos de error
        }
        else if (numBytesSent != numBytesRcvd) {
            log(ERROR, "send() sent unexpected number of bytes ");
            return -1;   // TODO definir codigos de error
        }

        // See if there is more data to receive
        numBytesRcvd = recv(clntSocket, buffer, BUFFSIZE, 0);
        if (numBytesRcvd < 0) {
            log(ERROR, "recv() failed");
            return -1;   // TODO definir codigos de error
        }
    }

    close(clntSocket); // cierra el socket
    return 0;
}

void clear(struct buffer *buffer) {
    free(buffer->buffer);
    buffer->buffer = NULL;
    buffer->from = buffer->len = 0;
}

void handleWrite(int socket, struct buffer *buffer, fd_set *writefds) {
    size_t bytesToSend = buffer->len - buffer->from;
    if (bytesToSend > 0) {  // Puede estar listo para enviar, pero no tenemos nada para enviar
        log(INFO, "Trying to send %zu bytes to socket %d\n", bytesToSend, socket);
        size_t bytesSent = send(socket, buffer->buffer + buffer->from, bytesToSend, MSG_DONTWAIT); // | MSG_NOSIGNAL
        log(INFO, "Sent %zu bytes\n", bytesSent);

        if (bytesSent < 0) {
            // Esto no deberia pasar ya que el socket estaba listo para escritura
            // TODO: manejar el error
            log(FATAL, "Error sending to socket %d", socket);
        } else {
            size_t bytesLeft = bytesSent - bytesToSend;

            // Si se pudieron mandar todos los bytes limpiamos el buffer y sacamos el fd para el select
            if (bytesLeft == 0) {
                clear(buffer);
                FD_CLR(socket, writefds);
            } else {
                buffer->from += bytesSent;
            }
        }
    }
}







