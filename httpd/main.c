#include <signal.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>

#include <tcp_utils.h>
#include <logger.h>
#include <client.h>
#include <io.h>
#include <args.h>
#include <monitor.h>
#include <selector_enums.h>
#include <proxy_stm.h>


static void sigterm_handler(const int signal);

void handle_creates(struct selector_key *key);


int main(int argc, char **argv) {

    // Read arguments and close stdin

    struct proxy_args args;
    parse_args(argc, argv, &args);

    close(0);

    // Register handlers for closing program appropiately

    signal(SIGTERM, sigterm_handler);
    signal(SIGINT,  sigterm_handler);

    // Start monitor on another thread

    char mng_port[6] = {0};
    snprintf(mng_port, 6, "%d", args.mng_port);

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, start_monitor, mng_port);

    // Start accepting connections

    char proxy_port[6] = {0};
    snprintf(proxy_port, 6, "%d", args.proxy_port);
      
    int serverSocket = create_tcp_server(proxy_port);
    if(serverSocket < 0) {
        log(ERROR, "Creating passive socket");
        exit(EXIT_FAILURE);
    }

    // Start handling connections

    int res = handle_connections(serverSocket, handle_creates);
    if(res < 0) {
        log(ERROR, "Handling connections");
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    close(serverSocket);

    return EXIT_SUCCESS;
    
}


void handle_creates(struct selector_key *key) {

    struct sockaddr_in address;
    int addrlen = sizeof(struct sockaddr_in);

    int masterSocket = key->s->fds[0].client_socket;


    // Accept the client connection

    int clientSocket = accept(masterSocket, (struct sockaddr *) &address, (socklen_t *) &addrlen);
    if (clientSocket < 0) {
        log(FATAL, "Accepting new connection")
        exit(EXIT_FAILURE);
    }

    key->item->client_socket = clientSocket;

    log(INFO, "New connection - FD: %d - IP: %s - Port: %d\n", clientSocket, inet_ntoa(address.sin_addr),
        ntohs(address.sin_port));

    if (strstr(proxy_conf.clientBlacklist, inet_ntoa(address.sin_addr)) != NULL) {
        log(INFO, "Kicking %s due to blacklist", inet_ntoa(address.sin_addr));
        item_kill(key->s, key->item);
        log(INFO, "Kicked %s due to blacklist", inet_ntoa(address.sin_addr));
    }

    // Initialize connection buffers and state machine

    buffer_init(&(key->item->read_buffer), CONN_BUFFER, malloc(CONN_BUFFER));
    buffer_init(&(key->item->write_buffer), CONN_BUFFER, malloc(CONN_BUFFER));

    memcpy(&(key->item->stm), &proto_stm, sizeof(proto_stm));
    stm_init(&(key->item->stm));

    key->item->client_interest = OP_READ;
    key->item->target_interest = OP_NOOP;
    selector_update_fdset(key->s, key->item);

}


static void sigterm_handler(const int signal) {

    printf("signal %d, cleaning up and exiting\n",signal);
    exit(EXIT_SUCCESS);

}

