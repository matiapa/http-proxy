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

int bsend(int socket, uint8_t *bytes, int len){
    int sentBytes = send(socket, bytes, len, 0);

    if(sentBytes != len)
        log(FATAL, "Failed to send message")
    
    return sentBytes;
}