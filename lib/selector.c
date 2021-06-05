/*
 * selector.c - un muliplexor de entrada salida
 */
#include <stdio.h>  // perror
#include <stdlib.h> // malloc
#include <string.h> // memset
#include <assert.h> // :)
#include <errno.h>  // :)
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <stdint.h> // SIZE_MAX
#include <fcntl.h>
#include <sys/select.h>
#include <signal.h>
#include <selector.h>
#include <logger.h>
#include <io.h>
#include <config.h>


#define N(x) (sizeof(x)/sizeof((x)[0]))

#define ERROR_DEFAULT_MSG "something failed"

/** retorna una descripción humana del fallo */
const char *
selector_error(const selector_status status) {
    const char *msg;
    switch(status) {
        case SELECTOR_SUCCESS:
            msg = "Success";
            break;
        case SELECTOR_ENOMEM:
            msg = "Not enough memory";
            break;
        case SELECTOR_MAXFD:
            msg = "Can't handle any more file descriptors";
            break;
        case SELECTOR_IARGS:
            msg = "Illegal argument";
            break;
        case SELECTOR_IO:
            msg = "I/O error";
            break;
        default:
            msg = ERROR_DEFAULT_MSG;
    }
    return msg;
}

#pragma GCC diagnostic ignored "-Wunused-parameter"
static void
wake_handler(const int signal) {
    // nada que hacer. está solo para interrumpir el select
}

// señal a usar para las notificaciones de resolución
struct selector_init conf;
static sigset_t emptyset, blockset;

selector_status
selector_init(const struct selector_init  *c) {
    memcpy(&conf, c, sizeof(conf));

    // inicializamos el sistema de comunicación entre threads y el selector
    // principal. La técnica se encuentra descripta en
    // "The new pselect() system call" <https://lwn.net/Articles/176911/>
    //  March 24, 2006
    selector_status   ret = SELECTOR_SUCCESS;
    struct sigaction act = {
        .sa_handler = wake_handler,
    };

    // 0. calculamos mascara para evitar que se interrumpa antes de llegar al
    //    select
    sigemptyset(&blockset);
    sigaddset  (&blockset, conf.signal);
    if(-1 == sigprocmask(SIG_BLOCK, &blockset, NULL)) {
        ret = SELECTOR_IO;
        goto finally;
    }

    // 1. Registramos una función que atenderá la señal de interrupción
    //    del selector.
    //    Esta interrupción es útil en entornos multi-hilos.

    if (sigaction(conf.signal, &act, 0)) {
        ret = SELECTOR_IO;
        goto finally;
    }
    sigemptyset(&emptyset);

finally:
    return ret;
}

selector_status
selector_close(void) {
    // Nada para liberar.
    // TODO(juan): podriamos reestablecer el handler de la señal.
    return SELECTOR_SUCCESS;
}



/* tarea bloqueante */
struct blocking_job {
    /** selector dueño de la resolucion */
    fd_selector  s;
    /** file descriptor dueño de la resolucion */
    int fd;

    /** datos del trabajo provisto por el usuario */
    void *data;

    /** el siguiente en la lista */
    struct blocking_job *next;
};

/** marca para usar en item->client_socket para saber que no está en uso */
static const int FD_UNUSED = -1;

/** verifica si el item está usado */
#define ITEM_USED(i) ( ( FD_UNUSED != (i)->client_socket) )


/** cantidad máxima de file descriptors que la plataforma puede manejar */
#define ITEMS_MAX_SIZE      FD_SETSIZE

// en esta implementación el máximo está dado por el límite natural de select(2).

/**
 * determina el tamaño a crecer, generando algo de slack para no tener
 * que realocar constantemente.
 */
static
size_t next_capacity(const size_t n) {
    unsigned bits = 0;
    size_t tmp = n;
    while(tmp != 0) {
        tmp >>= 1;
        bits++;
    }
    tmp = 1UL << bits;

    assert(tmp >= n);
    if(tmp > ITEMS_MAX_SIZE) {
        tmp = ITEMS_MAX_SIZE;
    }

    return tmp + 1;
}

static inline void
item_init(struct item *item) {
    item->client_socket = FD_UNUSED;
}

/**
 * inicializa los nuevos items. `last' es el indice anterior.
 * asume que ya está blanqueada la memoria.
 */
static void
items_init(fd_selector s, const size_t last) {
    assert(last <= s->fd_size);
    for(size_t i = last; i < s->fd_size; i++) {
        item_init(s->fds + i);
    }
}

/**
 * Mata el item
 */
void item_kill(fd_selector s, struct item * item) {
    struct sockaddr_in address;
    int addrlen = sizeof(struct sockaddr_in);
    item->client_socket = -1;
    getpeername(item->client_socket, (struct sockaddr*) &address, (socklen_t*) &addrlen);
    log(INFO, "Closed connection - IP: %s - Port: %d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));

    close(item->client_socket);
    close(item->target_socket);

    // Release connection buffers

    free(item->conn_buffer.data);

    FD_CLR(item->client_socket, &s->slave_r);
    FD_CLR(item->target_socket, &s->slave_w);
}

/**
 * calcula el fd maximo para ser utilizado en select()
 */
static int
items_max_fd(fd_selector s) {
    int max = 0;
    for(int i = 0; i <= s->max_fd; i++) {
        struct item *item = s->fds + i;
        if(ITEM_USED(item)) {
            if(item->client_socket > max) {
                max = item->client_socket;
            }
        }
    }
    return max;
}


/***************************************************************
  Configures fdset based on item permissions
****************************************************************/
static void
items_update_fdset_for_fd(fd_selector s, const struct item * item) {

    FD_CLR(item->client_socket, &s->slave_r);
    FD_CLR(item->client_socket, &s->slave_w);
    FD_CLR(item->target_socket, &s->slave_r);
    FD_CLR(item->target_socket, &s->slave_w);

    if(ITEM_USED(item)) {
        if(item->client_interest & OP_READ)
            FD_SET(item->client_socket, &(s->slave_r));

        if(item->client_interest & OP_WRITE)
            FD_SET(item->client_socket, &(s->slave_w));

        if(item->target_interest & OP_READ)
            FD_SET(item->target_socket, &(s->slave_r));

        if(item->target_interest & OP_WRITE)
            FD_SET(item->target_socket, &(s->slave_w));
    }
    
}


static selector_status
ensure_capacity(fd_selector s, const size_t n) {
    selector_status ret = SELECTOR_SUCCESS;

    const size_t element_size = sizeof(*s->fds);
    if(n < s->fd_size) {
        // nada para hacer, entra...
        ret = SELECTOR_SUCCESS;
    } else if(n > ITEMS_MAX_SIZE) {
        // me estás pidiendo más de lo que se puede.
        ret = SELECTOR_MAXFD;
    } else if(NULL == s->fds) {
        // primera vez.. alocamos
        const size_t new_size = next_capacity(n);

        s->fds = calloc(new_size, element_size);
        if(NULL == s->fds) {
            ret = SELECTOR_ENOMEM;
        } else {
            s->fd_size = new_size;
            items_init(s, 0);
        }
    } else {
        // hay que agrandar...
        const size_t new_size = next_capacity(n);
        if (new_size > SIZE_MAX/element_size) { // ver MEM07-C
            ret = SELECTOR_ENOMEM;
        } else {
            struct item *tmp = realloc(s->fds, new_size * element_size);
            if(NULL == tmp) {
                ret = SELECTOR_ENOMEM;
            } else {
                s->fds     = tmp;
                const size_t old_size = s->fd_size;
                s->fd_size = new_size;

                items_init(s, old_size);
            }
        }
    }

    return ret;
}


fd_selector
selector_new(const size_t initial_elements, const char * targetHost, const char * targetPost) {
    size_t size = sizeof(struct fdselector);
    fd_selector ret = malloc(size);
    if(ret != NULL) {
        memset(ret, 0x00, size);
        ret->master_t.tv_sec  = conf.select_timeout.tv_sec;
        ret->master_t.tv_nsec = conf.select_timeout.tv_nsec;
        assert(ret->max_fd == 0);
        ret->resolution_jobs  = 0;
        pthread_mutex_init(&ret->resolution_mutex, 0);
        if(0 != ensure_capacity(ret, initial_elements)) {
            selector_destroy(ret);
            ret = NULL;
        }

        // add port and host of target
        ret->targetHost = targetHost;
        ret->targetPort = targetPost;
    }
    return ret;
}


void
selector_destroy(fd_selector s) {
    // lean ya que se llama desde los casos fallidos de _new.
    if(s != NULL) {
        if(s->fds != NULL) {
            for(size_t i = 0; i < s->fd_size ; i++) {
                if(ITEM_USED(s->fds + i)) {
                    selector_unregister_fd(s, i);
                }
            }
            pthread_mutex_destroy(&s->resolution_mutex);
            for(struct blocking_job *j = s->resolution_jobs; j != NULL;
                j = j->next) {
                free(j);
            }
            free(s->fds);
            s->fds     = NULL;
            s->fd_size = 0;
        }
        free(s);
    }
}

#define INVALID_FD(fd)  ((fd) < 0 || (fd) >= ITEMS_MAX_SIZE)


/***************************************************************
  Registers a new fd, in the proxy case, the passive socket
****************************************************************/
selector_status
selector_register(
    fd_selector s, const int fd, const fd_handler  *handler,
    const fd_interest interest, void *data
) {
                         
    selector_status ret = SELECTOR_SUCCESS;

    if(s == NULL || INVALID_FD(fd) || handler == NULL) {
        ret = SELECTOR_IARGS;
        goto finally;
    }

    size_t ufd = (size_t)fd;
    if(ufd > s->fd_size) {
        ret = ensure_capacity(s, ufd);
        if(SELECTOR_SUCCESS != ret) {
            goto finally;
        }
    }

    struct item * item = s->fds + ufd;
    if(ITEM_USED(item)) {
        ret = SELECTOR_FDINUSE;
        goto finally;
    } else {
        item->client_socket = fd;

        if(fd > s->max_fd) {
            s->max_fd = fd;
        }
        items_update_fdset_for_fd(s, item);
    }
    
    s->handlers = *handler;

finally:
    return ret;

}


/***************************************************************
  Unregisters a fd, in the proxy case, the passive socket
****************************************************************/
selector_status
selector_unregister_fd(fd_selector s, const int fd) {

    selector_status ret = SELECTOR_SUCCESS;

    if(NULL == s || INVALID_FD(fd)) {
        ret = SELECTOR_IARGS;
        goto finally;
    }

    struct item *item = s->fds + fd;
    if(!ITEM_USED(item)) {
        ret = SELECTOR_IARGS;
        goto finally;
    }

    if(s->handlers.handle_close != NULL) {
        struct selector_key key = {
            .s    = s,
            .dst_socket   = item->client_socket,
        };
        s->handlers.handle_close(&key);
    }

    item->client_interest = OP_NOOP;
    items_update_fdset_for_fd(s, item);

    memset(item, 0x00, sizeof(*item));
    item_init(item);
    s->max_fd = items_max_fd(s);

finally:
    return ret;

}


/***************************************************************
  Handles the result of pselect, ie. availables reads/writes
****************************************************************/
static void
handle_iteration(fd_selector s) {

    int master_socket = s->fds[0].client_socket;
    int n = s->max_fd;

    struct selector_key key = {
        .s = s,
    };

    if (FD_ISSET(master_socket, &s->slave_r)) {

        // There is a new connection

        for (int i = 0; i < proxy_conf.maxClients; i++) {
            struct item *item = s->fds + i;

            if (ITEM_USED(item))
                continue;

            key.item = item;
            s->handlers.handle_create(&key);

            s->max_fd = item->client_socket > item->target_socket ? item->client_socket : item->target_socket;
            FD_CLR(master_socket, &s->slave_r);
            break;
        }

    } else {

        // A client/target demands attention

        for (int i = 0; i <= n; i++) {
            struct item *item = s->fds + i;
            key.item = item;

            if (ITEM_USED(item)) {
                
                // There is an initialized connection on this item

                // Check client -> target operations

                key.s = s;
                key.src_socket = item->client_socket;
                key.dst_socket = item->target_socket;

                if(FD_ISSET(item->client_socket, &s->slave_r)) {
                    // log(DEBUG, "Client read %d", item->client_socket)
                    if(OP_READ & item->client_interest) {
                        s->handlers.handle_read(&key);
                    }
                }

                if(FD_ISSET(item->target_socket, &s->slave_w)) {
                    // log(DEBUG, "Target write %d", item->target_socket)
                    if(OP_WRITE & item->target_interest) {
                        s->handlers.handle_write(&key);
                    }
                }

                // Check target -> client operations

                key.dst_socket = item->client_socket;
                key.src_socket = item->target_socket;

                if(FD_ISSET(item->target_socket, &s->slave_r)) {
                    // log(DEBUG, "Target read %d", item->target_socket)
                    if(OP_READ & item->target_interest) {
                        s->handlers.handle_read(&key);
                    }
                }

                if(FD_ISSET(item->client_socket, &s->slave_w)) {
                    // log(DEBUG, "Client write %d", item->client_socket)
                    if(OP_WRITE & item->client_interest) {
                        s->handlers.handle_write(&key);
                    }
                }

            }
        }

    }

}


static void
handle_block_notifications(fd_selector s) {
    struct selector_key key = {
        .s = s,
    };
    pthread_mutex_lock(&s->resolution_mutex);
    for(struct blocking_job *j = s->resolution_jobs;
        j != NULL ;
        j  = j->next) {

        struct item *item = s->fds + j->fd;
        if(ITEM_USED(item)) {
            key.src_socket   = item->client_socket;
            s->handlers.handle_block(&key);
        }

        free(j);
    }
    s->resolution_jobs = 0;
    pthread_mutex_unlock(&s->resolution_mutex);
}


selector_status
selector_notify_block(fd_selector s, const int fd) {

    selector_status ret = SELECTOR_SUCCESS;

    // TODO(juan): usar un pool
    struct blocking_job *job = malloc(sizeof(*job));
    if(job == NULL) {
        ret = SELECTOR_ENOMEM;
        goto finally;
    }
    job->s  = s;
    job->fd = fd;

    // encolamos en el selector los resultados
    pthread_mutex_lock(&s->resolution_mutex);
    job->next = s->resolution_jobs;
    s->resolution_jobs = job;
    pthread_mutex_unlock(&s->resolution_mutex);

    // notificamos al hilo principal
    pthread_kill(s->selector_thread, conf.signal);

finally:
    return ret;

}


/***************************************************************
  Begins the iteration awaiting for available reads/writes
****************************************************************/
selector_status
selector_select(fd_selector s) {
    selector_status ret = SELECTOR_SUCCESS;

    int masterSocket = s->fds[0].client_socket;
    FD_ZERO(&s->slave_r);
    FD_SET(masterSocket, &s->slave_r);

    int maxSocket = masterSocket;
    for (int i = 1; i < proxy_conf.maxClients; i++) {
        struct item * item = s->fds + i;

        if(!ITEM_USED(item))
            continue;

        FD_SET(item->client_socket, &s->slave_r);
        FD_SET(item->target_socket, &s->slave_r);

        int localMax = item->client_socket > item->target_socket ? item->client_socket : item->target_socket;
        maxSocket = localMax > maxSocket ? localMax : maxSocket;
    }


    s->selector_thread = pthread_self();
    int fds = pselect(s->max_fd + 1, &s->slave_r, &s->slave_w, 0, NULL, &emptyset); // sacar el NULL despues

    if(-1 == fds) { // entra si hubo un error
        switch(errno) {
            case EAGAIN:
            case EINTR:
                // si una señal nos interrumpio. ok!
                break;
            // case EBADF:
            //     // ayuda a encontrar casos donde se cierran los fd pero no
            //     // se desregistraron
            //     for(int i = 0 ; i < s->max_fd; i++) {
            //         if(FD_ISSET(i, &s->master_r)|| FD_ISSET(i, &s->master_w)) {
            //             if(-1 == fcntl(i, F_GETFD, 0)) {
            //                 fprintf(stderr, "Bad descriptor detected: %d\n", i);
            //             }
            //         }
            //     }
            //     ret = SELECTOR_IO;
            //     break;
            default:
                ret = SELECTOR_IO;
                goto finally;

        }
    } else {
        log(INFO, "Entered in iteration\n")
        handle_iteration(s);
    }
    if(ret == SELECTOR_SUCCESS) {
        handle_block_notifications(s);
    }
finally:
    return ret;
}


int
selector_fd_set_nio(const int fd) {
    int ret = 0;
    int flags = fcntl(fd, F_GETFD, 0);
    if(flags == -1) {
        ret = -1;
    } else {
        if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            ret = -1;
        }
    }
    return ret;
}
