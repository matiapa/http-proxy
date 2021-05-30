#include <string.h>
#include <sys/socket.h>
#include "../include/logger.h"
#include "../include/io.h"

int ssend(int socket, char *message){
    int sentBytes = send(socket, message, strlen(message), 0);

    if(sentBytes != strlen(message))
        log(FATAL, "Failed to send message")
    
    return sentBytes;
}