/*
 * selector.c - un muliplexor de entrada salida
 */
#include <stdio.h>  // perror
#include <stdlib.h> // malloc
#include <string.h> // memset
#include <assert.h> // :)
#include <errno.h>  // :)
#include <pthread.h>

#include <arpa/inet.h>

#include <stdint.h> // SIZE_MAX
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <signal.h>
#include "../include/selector.h"
#include "../include/logger.h"
#include "../include/server.h"
#include "../include/client.h"
#include "../include/io.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

#define ERROR_DEFAULT_MSG "something failed"

#define CONN_BUFFER 1024 // tamaño del buffer

#define MAX_CONN 1000

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

/** marca para usar en item->src_socket para saber que no está en uso */
static const int FD_UNUSED = -1;

/** verifica si el item está usado */
#define ITEM_USED(i) ( ( FD_UNUSED != (i)->src_socket) )


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
    item->src_socket = FD_UNUSED;
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
 * calcula el fd maximo para ser utilizado en select()
 */
static int
items_max_fd(fd_selector s) {
    int max = 0;
    for(int i = 0; i <= s->max_fd; i++) {
        struct item *item = s->fds + i;
        if(ITEM_USED(item)) {
            if(item->src_socket > max) {
                max = item->src_socket;
            }
        }
    }
    return max;
}

// configura los atributos del item, setea los readfds y writefds adecuadamente
static void
items_update_fdset_for_fd(fd_selector s, const struct item * item) {
    FD_CLR(item->src_socket, &s->master_r);
    FD_CLR(item->src_socket, &s->master_w);

    if(ITEM_USED(item)) {
        if(item->interest & OP_READ) {
            FD_SET(item->src_socket, &(s->master_r));
        }

        if(item->interest & OP_WRITE) {
            FD_SET(item->src_socket, &(s->master_w));
        }
    }
}

/**
 * garantizar cierta cantidad de elemenos en `fds'.
 * Se asegura de que `n' sea un número que la plataforma donde corremos lo
 * soporta
 */
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

// se usa para crear el socket master
selector_status
selector_register(fd_selector        s,
                     const int          fd,
                     const fd_handler  *handler,
                     const fd_interest  interest,
                     void *data) {
    selector_status ret = SELECTOR_SUCCESS;
    // 0. validación de argumentos
    if(s == NULL || INVALID_FD(fd) || handler == NULL) {
        ret = SELECTOR_IARGS;
        goto finally;
    }
    // 1. tenemos espacio?
    size_t ufd = (size_t)fd;
    if(ufd > s->fd_size) {
        ret = ensure_capacity(s, ufd);
        if(SELECTOR_SUCCESS != ret) {
            goto finally;
        }
    }

    // 2. registración
    struct item * item = s->fds + ufd;
    if(ITEM_USED(item)) {
        ret = SELECTOR_FDINUSE;
        goto finally;
    } else {
        item->src_socket       = fd;
        item->handler  = handler;
        item->interest = interest;
        item->data     = data;

        // actualizo colaterales
        if(fd > s->max_fd) {
            s->max_fd = fd;
        }
        items_update_fdset_for_fd(s, item);

        buffer_init(&(item->src_buffer), CONN_BUFFER, malloc(CONN_BUFFER));
        buffer_init(&(item->dst_buffer), CONN_BUFFER, malloc(CONN_BUFFER));
    }
    // guardo los handlers
    s->handlers = *handler;

finally:
    return ret;
}

selector_status
selector_unregister_fd(fd_selector       s,
                       const int         fd) {
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

    if(item->handler->handle_close != NULL) {
        struct selector_key key = {
            .s    = s,
            .src_socket   = item->src_socket,
            //.data = item->data,
        };
        item->handler->handle_close(&key);
    }

    item->interest = OP_NOOP;
    items_update_fdset_for_fd(s, item);

    memset(item, 0x00, sizeof(*item));
    item_init(item);
    s->max_fd = items_max_fd(s);

finally:
    return ret;
}

selector_status
selector_set_interest(fd_selector s, int fd, fd_interest i) {
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
    item->interest = i;
    items_update_fdset_for_fd(s, item);
finally:
    return ret;
}

selector_status
selector_set_interest_key(struct selector_key *key, fd_interest i) {
    selector_status ret;

    if(NULL == key || NULL == key->s || INVALID_FD(key->src_socket)) {
        ret = SELECTOR_IARGS;
    } else {
        ret = selector_set_interest(key->s, key->src_socket, i);
    }

    return ret;
}

/**
 * se encarga de manejar los resultados del select.
 * se encuentra separado para facilitar el testing
 */
static void
handle_iteration(fd_selector s) {
    int masterSocket = s->fds->src_socket;
    int n = s->max_fd;
    struct selector_key key = {
        .s = s,
    };

    for (int i = 0; i <= n; i++) {
        struct item *item = s->fds + i; // devuelve el items[i]
        if(ITEM_USED(item)) { // si el item esta en uso

            // Caso en el que se crea una nueva conexión
            if (FD_ISSET(masterSocket, &s->slave_r)) {
                key.item = item;
                s->handlers.handle_create(&key);
                s->max_fd = item->src_socket > item->dst_socket ? item->src_socket : item->dst_socket;
                FD_CLR(masterSocket, &s->slave_r);
                continue;
            }

            key.s = s;
            key.src_socket   = item->src_socket;
            key.dst_socket = item->dst_socket;
            key.src_buffer = &(item->src_buffer);
            key.dst_buffer = &(item->dst_buffer);
            // operaciones que van de src -> dst
            if(FD_ISSET(item->src_socket, &s->slave_r)) {
                if(OP_READ & item->interest) {
                    if(0 == item->handler->handle_read) {
                        assert(("OP_READ arrived but no handler. bug!" == 0));
                    } else {
                        item->handler->handle_read(&key);
                    }
                }
            }
            if(FD_ISSET(item->dst_socket, &s->slave_w)) {
                if(OP_WRITE & item->interest) {
                    if(0 == item->handler->handle_write) {
                        assert(("OP_WRITE arrived but no handler. bug!" == 0));
                    } else {
                        item->handler->handle_write(&key);
                    }
                }
            }


            key.dst_socket   = item->src_socket;
            key.src_socket = item->dst_socket;
            key.dst_buffer = &(item->src_buffer);
            key.src_buffer = &(item->dst_buffer);
            // operaciones que van de dst -> src
            if(FD_ISSET(item->dst_socket, &s->slave_r)) {
                if(OP_READ & item->interest) {
                    if(0 == item->handler->handle_read) {
                        assert(("OP_READ arrived but no handler. bug!" == 0));
                    } else {
                        item->handler->handle_read(&key);
                    }
                }
            }
            if(FD_ISSET(item->src_socket, &s->slave_w)) {
                if(OP_WRITE & item->interest) {
                    if(0 == item->handler->handle_write) {
                        assert(("OP_WRITE arrived but no handler. bug!" == 0));
                    } else {
                        item->handler->handle_write(&key);
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
            key.src_socket   = item->src_socket;
            // key.data = item->data;
            item->handler->handle_block(&key);
        }

        free(j);
    }
    s->resolution_jobs = 0;
    pthread_mutex_unlock(&s->resolution_mutex);
}


selector_status
selector_notify_block(fd_selector  s,
                 const int    fd) {
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

selector_status
selector_select(fd_selector s) {
    selector_status ret = SELECTOR_SUCCESS;

    memcpy(&s->slave_r, &s->master_r, sizeof(s->slave_r));
    memcpy(&s->slave_w, &s->master_w, sizeof(s->slave_w));
    memcpy(&s->slave_t, &s->master_t, sizeof(s->slave_t));

    int masterSocket = s->fds[0].src_socket;
    FD_ZERO(&s->slave_r);
    FD_SET(masterSocket, &s->slave_r);

    int maxSocket = masterSocket;
    for (int i = 0; i < MAX_CONN; i++) {
        struct item * item = s->fds + i;

        if(!ITEM_USED(item))
            continue;

        FD_SET(item->src_socket, &s->slave_r);
        FD_SET(item->dst_socket, &s->slave_r);

        int localMax = item->src_socket > item->dst_socket ? item->src_socket : item->dst_socket;
        maxSocket = localMax > maxSocket ? localMax : maxSocket;
    }


    s->selector_thread = pthread_self();
    int fds = pselect(s->max_fd + 1, &s->slave_r, &s->slave_w, 0, &s->slave_t, &emptyset); // sacar el NULL despues

    if(-1 == fds) { // entra si hubo un error
        switch(errno) {
            case EAGAIN:
            case EINTR:
                // si una señal nos interrumpio. ok!
                break;
            case EBADF:
                // ayuda a encontrar casos donde se cierran los fd pero no
                // se desregistraron
                for(int i = 0 ; i < s->max_fd; i++) {
                    if(FD_ISSET(i, &s->master_r)|| FD_ISSET(i, &s->master_w)) {
                        if(-1 == fcntl(i, F_GETFD, 0)) {
                            fprintf(stderr, "Bad descriptor detected: %d\n", i);
                        }
                    }
                }
                ret = SELECTOR_IO;
                break;
            default:
                ret = SELECTOR_IO;
                goto finally;

        }
    } else if (FD_ISSET(masterSocket, &s->slave_r)) { // si el master socket tiene read = 1 => crea un nuevo item
        log(INFO, "Creating new client\n")
        for (int i = 0; i <= MAX_CONN; i++) {
            struct item *item = s->fds + i; // devuelve el items[i]
            if (!ITEM_USED(item)) {
                struct sockaddr_in address;
                int addrlen = sizeof(struct sockaddr_in);
                int clientSocket = accept(masterSocket, (struct sockaddr *) &address, (socklen_t *) &addrlen);
                if (clientSocket < 0) {
                    log(FATAL, "Accepting new connection")
                    exit(EXIT_FAILURE);
                }

                log(INFO, "New connection - FD: %d - IP: %s - Port: %d\n", clientSocket, inet_ntoa(address.sin_addr),
                    ntohs(address.sin_port));
                int targetSocket = setupClientSocket(s->targetHost, s->targetPort);
                if (targetSocket < 0) {
                    log(ERROR, "Failed to connect to target")
                }
                item->src_socket = clientSocket;
                item->interest = OP_READ + OP_WRITE;
                item->handler = &(s->handlers);
                item->dst_socket = targetSocket;
                buffer_init(&(item->src_buffer), CONN_BUFFER, malloc(CONN_BUFFER));
                buffer_init(&(item->dst_buffer), CONN_BUFFER, malloc(CONN_BUFFER));
                s->max_fd = item->src_socket > item->dst_socket ? item->src_socket : item->dst_socket;
                FD_CLR(masterSocket, &s->slave_r);
                break;
            }
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
