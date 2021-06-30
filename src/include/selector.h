#ifndef SELECTOR_H
#define SELECTOR_H

#include <address.h>
#include <sys/time.h>
#include <stdbool.h>
#include <pthread.h>

/**
 * selector.c - un muliplexor de entrada salida
 *
 * Un selector permite manejar en un único hilo de ejecución la entrada salida
 * de file descriptors de forma no bloqueante.
 *
 * Esconde la implementación final (select(2) / poll(2) / epoll(2) / ..)
 *
 * El usuario registra para un file descriptor especificando:
 *  1. un handler: provee funciones callback que manejarán los eventos de
 *     entrada/salida
 *  2. un interés: que especifica si interesa leer o escribir.
 *
 * Es importante que los handlers no ejecute tareas bloqueantes ya que demorará
 * el procesamiento del resto de los descriptores.
 *
 * Si el handler requiere bloquearse por alguna razón (por ejemplo realizar
 * una resolución de DNS utilizando getaddrinfo(3)), tiene la posiblidad de
 * descargar el trabajo en un hilo que notificará al selector que el resultado
 * del trabajo está disponible y se le presentará a los handlers durante
 * la iteración normal. Los handlers no se tienen que preocupar por la
 * concurrencia.
 *
 * Dicha señalización se realiza mediante señales, y es por eso que al
 * iniciar la librería `selector_init' se debe configurar una señal a utilizar.
 *
 * Los métodos retornan su estado (éxito / error) de forma uniforme.
 * Puede utilizar `selector_error' para obtener una representación humana
 * del estado. Si el valor es `SELECTOR_IO' puede obtener información adicional
 * en errno(3).
 *
 * El flujo de utilización de la librería es:
 *  - iniciar la libreria `selector_init'
 *  - crear un selector: `selector_new'
 *  - registrar un file descriptor: `selector_register_fd'
 *  - esperar algún evento: `selector_iterate'
 *  - destruir los recursos de la librería `selector_close'
 */

#define INTEREST_OFF(FLAG, MASK)  ( (FLAG) & ~(MASK) )


/** ----------------------- STRUCTURES AND ENUMS ----------------------- */

typedef struct fdselector fdselector;

/** Function return values */
typedef enum selector_status {
    SELECTOR_SUCCESS  = 0,      // Successfull call
    SELECTOR_ENOMEM   = 1,      // Couldn't allocate memory
    SELECTOR_MAXFD    = 2,      // Limit of FDs reached
    SELECTOR_IARGS    = 3,      // Illegal argument
    SELECTOR_FDINUSE  = 4,      // Already used FD
    SELECTOR_IO       = 5,
} selector_status;

struct proxy_item;

/** Event handlers receive this structure as argument */
typedef struct selector_key {
    fdselector * s;         // The selector that activated the event
    int fd;                 // The file descriptor that activated the event
    struct proxy_item * data;            // Optional aditional data
} selector_key_t;

/** Event handlers for a specific fd */
typedef struct fd_handler {
    void (*handle_read)     (selector_key_t *key);
    void (*handle_write)    (selector_key_t *key);
    void (*handle_block)    (selector_key_t *key);
    void (*handle_close)    (selector_key_t *key);
} fd_handler;

/** Selector initialization options */
struct selector_init {
    /** Internal notification signal */
    const int signal;
    /** Iteration timeout */
    struct timespec select_timeout;
};

/**
 * Intereses sobre un file descriptor (quiero leer, quiero escribir, …)
 *
 * Son potencias de 2, por lo que se puede requerir una conjunción usando el OR
 * de bits.
 *
 * OP_NOOP es útil para cuando no se tiene ningún interés.
 */

typedef enum {
    OP_NOOP    = 0,
    OP_READ    = 1 << 0,
    OP_WRITE   = 1 << 2,
} fd_interest;

typedef enum {
    READ_BUFFER   = 1 << 0,
    WRITE_BUFFER  = 1 << 2
} rst_buffer;

/** ----------------------- METHOD PROTOTYPES ----------------------- */

/** Returns a human readable status description */
const char * selector_error(const selector_status status);

/** Selector library initialization */
selector_status selector_init(const struct selector_init *c);

/** Selector library destroy */
selector_status selector_close(void);

/* Selector instance creation, returns NULL on error  */
fdselector * selector_new(const size_t initial_elements);

/** Selector instance destruction, accepts NULL */
void selector_destroy(fdselector * s);

/** Registers a new file descriptor on selector */
selector_status selector_register(fdselector * s, const int fd, const fd_handler  *handler,
    const fd_interest interest, void *data);

/** Unregisters a new file descriptor on selector */
selector_status selector_unregister_fd(fdselector * s, const int fd);

/** Allows changing an fd interests */
selector_status selector_set_interest(fdselector * s, int fd, fd_interest i);

/** Blocks until there is a new event or timeout is reached, then iterates again */
selector_status selector_select(fdselector * s);

/** Notifies a blocking job finished */
selector_status selector_notify_block(fdselector * s, const int fd);

/** Sets O_NONBLOCK on fd, returns -1 on error and sets ERRNO */
int selector_fd_set_nio(const int fd);

#endif
