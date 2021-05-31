/**
 * main.c - servidor proxy socks concurrente
 *
 * Interpreta los argumentos de línea de comandos, y monta un socket
 * pasivo.
 *
 * Todas las conexiones entrantes se manejarán en éste hilo.
 *
 * Se descargará en otro hilos las operaciones bloqueantes (resolución de
 * DNS utilizando getaddrinfo), pero toda esa complejidad está oculta en
 * el selector.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>

#include <unistd.h>
#include <sys/types.h>   // socket
#include <sys/socket.h>  // socket
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "../include/selector.h"
#include "../include/buffer.h"
#include "../include/io.h"
#include "../include/logger.h"
#include "../include/server.h"
#include "../include/client.h"

static bool done = false;

static void
sigterm_handler(const int signal) {
    printf("signal %d, cleaning up and exiting\n",signal);
    done = true;
}

void handle_writes_main(struct selector_key *key) {

    if (buffer_can_read(key->src_buffer)) {
        size_t len;
        uint8_t *bytes = buffer_read_ptr(key->src_buffer, &len);

        int sentBytes = bsend(key->dst_socket, bytes, len);

        buffer_read_adv(key->src_buffer, sentBytes);
        FD_CLR(key->dst_socket, &(key->s)->slave_w);
    }
}

void handle_reads_main(struct selector_key *key) {

    if (buffer_can_write(key->src_buffer)) {

        size_t space;
        uint8_t *ptr = buffer_write_ptr(key->src_buffer, &space);

        int readBytes = read(key->src_socket, ptr, space);

        buffer_write_adv(key->src_buffer, readBytes);

        if (readBytes == 0) {
            selector_unregister_fd(key->s, key->src_socket);
            selector_unregister_fd(key->s, key->dst_socket);
        } else {
            log(DEBUG, "Received %d bytes from socket %d\n", readBytes, key->src_socket);
            FD_SET(key->dst_socket, &(key->s)->slave_w);
        }
    }
}

void handle_create_main(struct selector_key *key) {
    struct sockaddr_in address;
    int addrlen = sizeof(struct sockaddr_in);
    int masterSocket = key->s->fds->src_socket;
    int clientSocket = accept(masterSocket, (struct sockaddr *) &address, (socklen_t *) &addrlen);
    if (clientSocket < 0) {
        log(FATAL, "Accepting new connection")
        exit(EXIT_FAILURE);
    }

    log(INFO, "New connection - FD: %d - IP: %s - Port: %d\n", clientSocket, inet_ntoa(address.sin_addr),
        ntohs(address.sin_port));
    int targetSocket = setupClientSocket(key->s->targetHost, key->s->targetPort);
    if (targetSocket < 0) {
        log(ERROR, "Failed to connect to target")
    }
    key->item->src_socket = clientSocket;
    key->item->dst_socket = targetSocket;
    buffer_init(&(key->item->src_buffer), CONN_BUFFER, malloc(CONN_BUFFER));
    buffer_init(&(key->item->dst_buffer), CONN_BUFFER, malloc(CONN_BUFFER));
}

int main(const int argc, const char **argv) {
    unsigned port;

    if(argc == 4) {
        char *end     = 0;
        const long sl = strtol(argv[1], &end, 10);
        targetHost = argv[2];
        targetPort = argv[3];

        if (end == argv[1]|| '\0' != *end
            || ((LONG_MIN == sl || LONG_MAX == sl) && ERANGE == errno)
            || sl < 0 || sl > USHRT_MAX) {
            fprintf(stderr, "port should be an integer: %s\n", argv[1]);
            return 1;
        }
        port = sl;
    } else {
        fprintf(stderr, "Usage: %s <LISTEN_PORT> <TARGET_HOST> <TARGET_PORT>\n", argv[0]);
        return 1;
    }

    // no tenemos nada que leer de stdin
    close(0);

    const char       *err_msg = NULL;
    selector_status   ss      = SELECTOR_SUCCESS;
    fd_selector selector      = NULL;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET; // ipv4
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    const int server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(server < 0) {
        err_msg = "unable to create socket";
        goto finally;
    }

    fprintf(stdout, "Listening on TCP port %d\n", port);

    // man 7 ip. no importa reportar nada si falla.
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));

    if(bind(server, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        err_msg = "unable to bind socket";
        goto finally;
    }

    if (listen(server, 20) < 0) { // 20 clients in the line
        err_msg = "unable to listen";
        goto finally;
    }

    // registrar sigterm es útil para terminar el programa normalmente.
    // esto ayuda mucho en herramientas como valgrind.
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT,  sigterm_handler);

    if(selector_fd_set_nio(server) == -1) {
        err_msg = "getting server socket flags";
        goto finally;
    }
    const struct selector_init conf = {
            .signal = SIGALRM,
            .select_timeout = {
                    .tv_sec  = 10,
                    .tv_nsec = 0,
            },
    };
    if(0 != selector_init(&conf)) {
        err_msg = "initializing selector";
        goto finally;
    }

    selector = selector_new(1024, targetHost, targetPort);
    if(selector == NULL) {
        err_msg = "unable to create selector";
        goto finally;
    }

    // llena las funciones usadas de los handlers
    const struct fd_handler handlers = {
            .handle_read       = handle_reads_main, // handler para read
            .handle_write      = handle_writes_main, // handler para write
            .handle_create     = handle_create_main, // handler para create client
            .handle_close      = NULL, // nada que liberar
    };
    // crea el socket master
    ss = selector_register(selector, server, &handlers, OP_READ + OP_WRITE, NULL);

    if(ss != SELECTOR_SUCCESS) {
        err_msg = "registering fd";
        goto finally;
    }

    // while(1)
    for(;!done;) {
        err_msg = NULL;
        ss = selector_select(selector);
        if(ss != SELECTOR_SUCCESS) {
            err_msg = "serving";
            goto finally;
        }
    }
    if(err_msg == NULL) {
        err_msg = "closing";
    }

    int ret = 0;
    finally:
    if(ss != SELECTOR_SUCCESS) {
        fprintf(stderr, "%s: %s\n", (err_msg == NULL) ? "": err_msg,
                ss == SELECTOR_IO
                ? strerror(errno)
                : selector_error(ss));
        ret = 2;
    } else if(err_msg) {
        perror(err_msg);
        ret = 1;
    }
    if(selector != NULL) {
        selector_destroy(selector);
    }
    selector_close();

    // socksv5_pool_destroy(); // es de el archivo socks5.h

    if(server >= 0) {
        close(server);
    }
    return ret;
}

