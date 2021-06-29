#ifndef CLIENT_ARGS_H
#define CLIENT_ARGS_H

#include <stdbool.h>

struct client_args {
    char * monitor_addr;
    unsigned short monitor_port;
    unsigned short client_port;
};

/**
 * Parses command line arguments, leaving them on proxy_args structure
 */
void client_parse_args(const int argc, char **argv, struct client_args *args);

#endif

