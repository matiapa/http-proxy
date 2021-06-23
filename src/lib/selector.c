/*
 * selector.c - un muliplexor de entrada salida
 */
#include <stdio.h>
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
#include <config.h>
#include <statistics.h>

#define N(x) (sizeof(x)/sizeof((x)[0]))

#define ERROR_DEFAULT_MSG "something failed"

/** retorna una descripción humana del fallo */
const char * selector_error(const selector_status status) {
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
static void wake_handler(const int signal) {
    // nada que hacer. está solo para interrumpir el select
}

// señal a usar para las notificaciones de resolución
struct selector_init conf;
static sigset_t emptyset, blockset;

selector_status selector_init(const struct selector_init  *c) {
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

selector_status selector_close(void) {
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
static size_t next_capacity(const size_t n) {
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

static inline void item_init(struct item *item) {
    item->client_socket = FD_UNUSED;
    item->target_socket = FD_UNUSED;
}

/**
 * inicializa los nuevos items. `last' es el indice anterior.
 * asume que ya está blanqueada la memoria.
 */
static void items_init(fd_selector s, const size_t last) {
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

    if (item->client_socket !=  s->fds[0].client_socket) {
        if (getpeername(item->client_socket, (struct sockaddr*) &address, (socklen_t*) &addrlen) == 0) {
            char * ip_str = inet_ntoa(address.sin_addr);
            if (ip_str == NULL)
                return;
            log(INFO, "\x1b[1;33mDisconnected client: %s:%d (FD: %d)\n\x1b[1;0m", inet_ntoa(address.sin_addr),
                ntohs(address.sin_port), item->client_socket);
        }
    }
    
    // cuando no es el master socket
    if (item->read_buffer.write != NULL && item->read_buffer.read != NULL) {
        buffer_reset(&(item->read_buffer));
        buffer_reset(&(item->write_buffer));

        
        close(item->target_socket);

        // Release connection buffers

        free_buffer(&item->read_buffer);
        free_buffer(&item->write_buffer);
        free_buffer(&item->req_parser.parse_buffer);

       
        // Marks item as unused
        FD_CLR(item->target_socket, &s->master_r);
        FD_CLR(item->target_socket, &s->master_w);

        
    }

    close(item->client_socket);

    // Marks item as unused
    FD_CLR(item->client_socket, &s->master_r);
    FD_CLR(item->client_socket, &s->master_w);

    item->client_socket = FD_UNUSED;
    item->target_socket = FD_UNUSED;
    
}

/**
 * calcula el fd maximo para ser utilizado en select()
 */
static int items_max_fd(fd_selector s) {
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


static selector_status ensure_capacity(fd_selector s, const size_t n) {
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
        if (new_size > SIZE_MAX/element_size) { //-V547
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


fd_selector selector_new(const size_t initial_elements, const fd_handler *handler) {
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
        ret->handlers = *handler;
        ret->master_size = 0;
    }
    return ret;
}


void selector_destroy(fd_selector s) {
    // lean ya que se llama desde los casos fallidos de _new.
    if(s != NULL) {
        if(s->fds != NULL) {
            for(size_t i = 0; i < s->fd_size ; i++) {
                if(ITEM_USED(s->fds + i)) {
                    selector_unregister_fd(s, (int)i);
                }
            }
            pthread_mutex_destroy(&s->resolution_mutex);
            for(struct blocking_job *j = s->resolution_jobs; j != NULL; ) {
                struct blocking_job * aux = j->next;
                free(j);
                j = aux;
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
  Configures fdset based on item permissions
****************************************************************/
void selector_update_fdset(fd_selector s, const struct item * item) {
    if(ITEM_USED(item)) {

        // Check that client socket is set

        if (item->client_socket != 0) {
            // log(DEBUG, "Updating fd_sets for client %d", item->client_socket)

            FD_CLR(item->client_socket, &(s->master_r));
            FD_CLR(item->client_socket, &(s->master_w));

            if(item->client_interest & OP_READ)
                FD_SET(item->client_socket, &(s->master_r));                 
            
            if(item->client_interest & OP_WRITE)
                FD_SET(item->client_socket, &(s->master_w));  
        }

        // Check that target socket is set

        if (item->target_socket != 0) {
            // log(DEBUG, "Updating fd_sets for target %d", item->target_socket)

            FD_CLR(item->target_socket, &(s->master_w));
            FD_CLR(item->target_socket, &(s->master_r));

            if(item->target_interest & OP_READ)
                FD_SET(item->target_socket, &(s->master_r));             

            if(item->target_interest & OP_WRITE)
                FD_SET(item->target_socket, &(s->master_w));   
        }

        // log(DEBUG, "New sets: read_fd[0] = %d | read_fd[4] = %d | read_fd[5] = %d",
        //     FD_ISSET(0, &(s->master_r)), FD_ISSET(4, &(s->master_r)), FD_ISSET(5, &(s->master_r)));
        // log(DEBUG, "New sets: write_fd[0] = %d | write_fd[4] = %d | write_fd[5] = %d",
        //     FD_ISSET(0, &(s->master_w)), FD_ISSET(4, &(s->master_w)), FD_ISSET(5, &(s->master_w)));

    }
}


/***************************************************************
  Registers a new fd, in the proxy case, the passive socket
****************************************************************/
selector_status selector_register(fd_selector s, const int fd, const fd_interest interest, void *data) {
                         
    selector_status ret = SELECTOR_SUCCESS;

    if (s == NULL || INVALID_FD(fd)) {
        ret = SELECTOR_IARGS;
        goto finally;
    }

    for (int i = 0; i < MASTER_SOCKET_SIZE; i++) {
        struct item * item = s->fds + i;
        if (ITEM_USED(item)) {
            ret = SELECTOR_FDINUSE;
            continue;
        } else {
            item->client_socket   = fd;
            item->client_interest = interest;
            item->data   = data;

            log(DEBUG, "Registered socket %d with interests %d", fd, interest);

            if(fd > s->max_fd)
                s->max_fd = fd;

            FD_CLR(item->client_socket, &(s->master_r));
            FD_CLR(item->client_socket, &(s->master_w));

            if(item->client_interest & OP_READ)
                FD_SET(item->client_socket, &(s->master_r));

            if(item->client_interest & OP_WRITE)
                FD_SET(item->client_socket, &(s->master_w));

            ret = SELECTOR_SUCCESS;
            s->masters[s->master_size++] = item;
            goto finally;
        }
    }

finally:
    return ret;

}


/***************************************************************
  Unregisters a fd, in the proxy case, the passive socket
****************************************************************/
selector_status selector_unregister_fd(fd_selector s, const int fd) {

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

    if (s->handlers.handle_close != NULL) {
        struct selector_key key = {
            .s    = s,
            .item = item
        };
        s->handlers.handle_close(&key);
    }

    item->client_interest = OP_NOOP;
    selector_update_fdset(s, item);

    memset(item, 0x00, sizeof(*item));
    item_init(item);
    s->max_fd = items_max_fd(s);

finally:
    return ret;

}

/***************************************************************
  Handles the result of pselect, ie. availables reads/writes
****************************************************************/
static void handle_iteration(fd_selector s) {

    // Check for item timeouts

    for (size_t i = 1; i < proxy_conf.maxClients; i++) {
        struct item * item = s->fds + i;

        if(!ITEM_USED(item))
            continue;

        int delta = time(NULL) - item->last_activity;
        if(proxy_conf.connectionTimeout > 0 && proxy_conf.connectionTimeout < delta){
            log(INFO, "\x1b[1;33mKicking %d due to timeout\x1b[1;0m", item->client_socket);
            item_kill(s, item);
        }
    }
    
    int n = s->max_fd;
    int flag = 0;

    struct selector_key key = { .s = s };

    for (size_t i = 0; i < s->master_size && !flag; i++) {
        int master_socket = s->masters[i]->client_socket;
        if (FD_ISSET(s->masters[i]->client_socket, &s->slave_r)) {
            for (size_t j = MASTER_SOCKET_SIZE; j < proxy_conf.maxClients; j++) {
                struct item *item = s->fds + j;

                if (ITEM_USED(item))
                    continue;

                key.item = item;
                item->master_socket = master_socket;

                s->handlers.handle_create(&key);

                s->max_fd = item->client_socket > item->target_socket
                                ? item->client_socket
                                : item->target_socket;
                break;
            }
            flag = 1;
        }
    }

    if (!flag) {

        // A client/target demands attention

        for (int i = MASTER_SOCKET_SIZE; i <= n; i++) {
            struct item *item = s->fds + i;
            key.item = item;

            if (ITEM_USED(item)) {
                
                // There is an initialized connection on this item

                key.s = s;

                // Check read operations

                if(FD_ISSET(item->client_socket, &s->slave_r)) {
                    log(DEBUG, "Client %d has read available", item->client_socket)
                    if(OP_READ & item->client_interest) {
                        key.active_fd = item->client_socket;
                        stm_handler_read(&(item->stm), &key);
                        continue;
                    }
                }

                if(FD_ISSET(item->target_socket, &s->slave_r)) {
                    log(DEBUG, "Target %d has read available", item->target_socket)
                    if(OP_READ & item->target_interest) {
                        key.active_fd = item->target_socket;
                        stm_handler_read(&(item->stm), &key);
                        continue;
                    }
                }

                // Check write operations

                if(FD_ISSET(item->client_socket, &s->slave_w)) {
                    log(DEBUG, "Client %d has write available", item->client_socket)
                    if(OP_WRITE & item->client_interest) {
                        key.active_fd = item->client_socket;
                        stm_handler_write(&(item->stm), &key);
                        continue;
                    }
                }

                if(FD_ISSET(item->target_socket, &s->slave_w)) {
                    log(DEBUG, "Target %d has write available", item->target_socket)
                    if(OP_WRITE & item->target_interest) {
                        key.active_fd = item->target_socket;
                        stm_handler_write(&(item->stm), &key);
                        continue;
                    }
                }

            }
        }
    }

}


static void handle_block_notifications(fd_selector s) {
    struct selector_key key = {
        .s = s,
    };
    pthread_mutex_lock(&s->resolution_mutex);
    for(struct blocking_job *j = s->resolution_jobs; j != NULL ;) {

        struct item * item;
        for (int i = 0; i <= s->max_fd; i++) {
            item = s->fds + i;
            if (item->client_socket == j->fd)
                break;
        }

        if(ITEM_USED(item)) {
            key.item = item;
            stm_handler_block(&(item->stm), &key);
        }
        struct blocking_job * aux = j->next;
        free(j);
        j = aux;
    }
    s->resolution_jobs = 0;
    pthread_mutex_unlock(&s->resolution_mutex);
}


selector_status selector_notify_block(fd_selector s, const int fd) {

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
selector_status selector_select(fd_selector s) {
    selector_status ret = SELECTOR_SUCCESS;

    memcpy(&s->slave_r, &s->master_r, sizeof(s->slave_r));
    memcpy(&s->slave_w, &s->master_w, sizeof(s->slave_w));
    memcpy(&s->slave_t, &s->master_t, sizeof(s->slave_t));

    // Calculate max socket

    int maxSocket = 0;
    for (size_t i = 0; i < proxy_conf.maxClients; i++) {
        struct item * item = s->fds + i;

        if(!ITEM_USED(item))
            continue;

        int localMax = item->client_socket > item->target_socket ? item->client_socket : item->target_socket;
        maxSocket = localMax > maxSocket ? localMax : maxSocket;
    }

    s->max_fd = maxSocket;
        
    struct timespec t = { .tv_sec = SELECTOR_TIMEOUT_SECS };

    s->selector_thread = pthread_self();
    int fds = pselect(s->max_fd + 1, &s->slave_r, &s->slave_w, 0, &t, &emptyset); // sacar el NULL despues

    log(DEBUG, "Exited pselect() with %d", fds);

    if(-1 == fds) {
        switch(errno) {
            case EAGAIN:
            case EINTR:
                log(DEBUG,"interrupted pselect");
                break;
            default:
                printf("%s\n", strerror(errno));
                ret = SELECTOR_IO;
                goto finally;

        }
    } else {
        log(DEBUG, "Handling iteration")
        handle_iteration(s);
    }
    
    handle_block_notifications(s);

finally:
    return ret;
}


int selector_fd_set_nio(const int fd) {
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
