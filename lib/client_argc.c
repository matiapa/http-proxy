#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include "../include/client_argc.h"


static void version(void) {
    printf(
            "client version 0.0\n"
            "ITBA Protocolos de Comunicación 2021/1 -- Grupo 6\n"
    );
}


static unsigned short port(const char *s) {
    char *end     = 0;
    const long sl = strtol(s, &end, 10);

    if (
            end == s
            || *end != '\0'
            || ((LONG_MIN == sl || LONG_MAX == sl) && ERANGE == errno)
            || sl < 0 || sl > USHRT_MAX
            ) {
        fprintf(stderr, "Port should be in the range of 1-65536: %s\n", s);
        exit(1);
        return 1;
    }

    return (unsigned short) sl;
}


static void
usage(const char *progname) {
    fprintf(
            stderr,
            "Usage: %s [OPTION]...\n"
            "\n"
            "   -h                      Imprime la ayuda y termina.\n"
            "   -l <management addr>    Dirección a del management.\n"
            "   -p <management port>    Puerto a del management.\n"
            "   -P <client port>        Puerto a utilizar.\n"
            "   -v                      Imprime información sobre la versión versión y termina.\n"
            "\n",
            progname
    );
    exit(1);
}


void client_parse_args(const int argc, char **argv, struct client_args *args) {
    memset(args, 0, sizeof(*args));

    args->monitor_addr= "127.0.0.1";
    args->monitor_port = 9090;
    args->client_port = 9091;

    int c;

    while (true) {
        int option_index = 0;

        c = getopt_long(argc, argv, "hl:L:Np:P:u:v", NULL, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                usage(argv[0]);
                break;
            case 'l':
                args->monitor_addr = optarg;
                break;
            case 'p':
                args->monitor_port = port(optarg);
                break;
            case 'P':
                args->client_port = port(optarg);
                break;
            case 'v':
                version();
                exit(0);
            default:
                fprintf(stderr, "unknown argument %d.\n", c);
                exit(1);
        }

    }

    if (optind < argc) {
        fprintf(stderr, "argument not accepted: ");
        while (optind < argc) {
            fprintf(stderr, "%s ", argv[optind++]);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
}
