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
