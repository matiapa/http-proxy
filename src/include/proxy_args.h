#ifndef ARGS_H
#define ARGS_H

#include <stdbool.h>

struct doh {
    char           *host;
    char           *ip;
    unsigned short  port;
    char           *path;
    char           *query;
};

struct proxy_args {
    char *          proxy_addr;
    unsigned short  proxy_port;

    char *          mng_addr;
    unsigned short  mng_port;

    bool            disectors_enabled;

    struct doh      doh;
};

/**
 * Parses command line arguments, leaving them on proxy_args structure
 */
void 
parse_args(const int argc, char **argv, struct proxy_args *args);

#endif

