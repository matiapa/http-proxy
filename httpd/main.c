#include <signal.h>
#include <arpa/inet.h>

#include <tcp_utils.h>
#include <logger.h>
#include <client.h>
#include <io.h>
#include <args.h>
#include <monitor.h>


static void sigterm_handler(const int signal);

void handle_writes(struct selector_key *key);

void handle_reads(struct selector_key *key);

void handle_creates(struct selector_key *key);


int main(int argc, char **argv) {

    // Read arguments and close stdin

    struct proxy_args args;
    parse_args(argc, argv, &args);
    
    targetHost = "localhost";
    targetPort = "8081";

    close(0);

    // Register handlers for closing program appropiately

    signal(SIGTERM, sigterm_handler);
    signal(SIGINT,  sigterm_handler);

    // Start monitor on another process

    int pid = fork();
    if(pid == 0) {
        start_monitor("8082");
        log(FATAL, "Monitor process ended prematurely");
    } else if(pid < 0) {
        log(FATAL, "Failed to create monitor process");
    }

    // Start accepting connections

    char listenPort[6] = {0};
    snprintf(listenPort, 6, "%d", args.proxy_port);
      
    int serverSocket = create_tcp_server(listenPort);
    if(serverSocket < 0) {
        log(ERROR, "Creating passive socket");
        exit(EXIT_FAILURE);
    }

    // Start handling connections

    int res = handle_connections(serverSocket, handle_creates, handle_reads, handle_writes);
    if(res < 0) {
        log(ERROR, "Handling connections");
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    close(serverSocket);

    return EXIT_SUCCESS;
    
}


void handle_writes(struct selector_key *key) {

    if (buffer_can_read(key->src_buffer)) {

        size_t len;
        uint8_t *bytes = buffer_read_ptr(key->src_buffer, &len);

        int sentBytes = bsend(key->dst_socket, bytes, len);

        buffer_read_adv(key->src_buffer, sentBytes);
        FD_CLR(key->dst_socket, &(key->s)->slave_w);

    }

}


void handle_reads(struct selector_key *key) {

    if (buffer_can_write(key->src_buffer)) {

        size_t space;
        uint8_t *ptr = buffer_write_ptr(key->src_buffer, &space);

        int readBytes = read(key->src_socket, ptr, space);

        buffer_write_adv(key->src_buffer, readBytes);

        if (readBytes <= 0) {
            item_kill(key->s, key->item);
        } else {
            log(DEBUG, "Received %d bytes from socket %d\n", readBytes, key->src_socket);
            FD_SET(key->dst_socket, &(key->s)->slave_w);
        }

    }

}


void handle_creates(struct selector_key *key) {

    struct sockaddr_in address;
    int addrlen = sizeof(struct sockaddr_in);

    int masterSocket = key->s->fds[0].src_socket;

    // Accept the client connection

    int clientSocket = accept(masterSocket, (struct sockaddr *) &address, (socklen_t *) &addrlen);
    if (clientSocket < 0) {
        log(FATAL, "Accepting new connection")
        exit(EXIT_FAILURE);
    }

    log(INFO, "New connection - FD: %d - IP: %s - Port: %d\n", clientSocket, inet_ntoa(address.sin_addr),
        ntohs(address.sin_port));

    // Initiate a connection to target

    int targetSocket = setupClientSocket(key->s->targetHost, key->s->targetPort);
    if (targetSocket < 0) {
        log(ERROR, "Failed to connect to target")
    }

    key->item->src_socket = clientSocket;
    key->item->dst_socket = targetSocket;

    buffer_init(&(key->item->src_buffer), CONN_BUFFER, malloc(CONN_BUFFER));
    buffer_init(&(key->item->dst_buffer), CONN_BUFFER, malloc(CONN_BUFFER));

}


static void sigterm_handler(const int signal) {

    printf("signal %d, cleaning up and exiting\n",signal);
    exit(EXIT_SUCCESS);

}

