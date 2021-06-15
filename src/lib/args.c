#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include <args.h>


static void
version(void) {
    printf(
        "httpd version 0.0\n"
        "ITBA Protocolos de Comunicación 2021/1 -- Grupo 6\n"
    );
}


static unsigned short
port(const char *s) {
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
        "   -h                  Imprime la ayuda y termina.\n"
        "   -l <proxy addr>     Dirección donde servirá el proxy.\n"
        "   -L <conf  addr>     Dirección donde servirá el servicio de management.\n"
        "   -p <proxy port>     Puerto entrante del proxy.\n"
        "   -o <conf port>      Puerto entrante sel servicio de management.\n"
        "   -N                  Deshabilita los passwords disectors y termina.\n"
        "   -v                  Imprime información sobre la versión versión y termina.\n"
        "\n"
        "   --doh-ip    <ip>    Dirección del servidor DoH\n"
        "   --doh-port  <port>  Puerto del servidor DoH\n"
        "   --doh-host  <host>  Host del servidor DoH\n"
        "   --doh-path  <host>  Path del servidor DoH\n"
        "\n",
        progname
    );
    exit(1);
}


void
parse_args(const int argc, char **argv, struct proxy_args *args) {
    memset(args, 0, sizeof(*args));

    args->proxy_addr = NULL;
    args->proxy_port = 8080;

    args->mng_addr   = NULL;
    args->mng_port   = 9090;

    args->disectors_enabled = true;

    args->doh.host = "localhost";
    args->doh.ip   = "0.0.0.0";
    args->doh.port = 8053;
    args->doh.path = "/getnsrecord";

    int c;

    while (true) {
        int option_index = 0;
        static struct option long_options[] = {
            { "doh-ip",    required_argument, 0, 0xD001 },
            { "doh-port",  required_argument, 0, 0xD002 },
            { "doh-host",  required_argument, 0, 0xD003 },
            { "doh-path",  required_argument, 0, 0xD004 },
            { 0,           0,                 0, 0 }
        };

        c = getopt_long(argc, argv, "hl:L:Np:o:u:v", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                usage(argv[0]);
                break;
            case 'l':
                args->proxy_addr = optarg;
                break;
            case 'L':
                args->mng_addr = optarg;
                break;
            case 'N':
                args->disectors_enabled = false;
                break;
            case 'p':
                args->proxy_port = port(optarg);
                break;
            case 'o':
                args->mng_port   = port(optarg);
                break;
            case 'v':
                version();
                exit(0);
                break;
            case 0xD001:
                args->doh.ip = optarg;
                break;
            case 0xD002:
                args->doh.port = port(optarg);
                break;
            case 0xD003:
                args->doh.host = optarg;
                break;
            case 0xD004:
                args->doh.path = optarg;
                break;
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
