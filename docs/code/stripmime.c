#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>

#include "parser_utils.h"
#include "pop3_multi.h"
#include "mime_chars.h"
#include "mime_msg.h"

/*
 * imprime información de debuging sobre un evento.
 *
 * @param p        prefijo (8 caracteres)
 * @param namefnc  obtiene el nombre de un tipo de evento
 * @param e        evento que se quiere imprimir
 */
static void
debug(const char *p,
      const char * (*namefnc)(unsigned),
      const struct parser_event* e) {
    // DEBUG: imprime
    if (e->n == 0) {
        fprintf(stderr, "%-8s: %-14s\n", p, namefnc(e->type));
    } else {
        for (int j = 0; j < e->n; j++) {
            const char* name = (j == 0) ? namefnc(e->type)
                                        : "";
            if (e->data[j] <= ' ') {
                fprintf(stderr, "%-8s: %-14s 0x%02X\n", p, name, e->data[j]);
            } else {
                fprintf(stderr, "%-8s: %-14s %c\n", p, name, e->data[j]);
            }
        }
    }
}

/* mantiene el estado durante el parseo */
struct ctx {
    /* delimitador respuesta multi-línea POP3 */
    struct parser* multi;
    /* delimitador mensaje "tipo-rfc 822" */
    struct parser* msg;
    /* detector de field-name "Content-Type" */
    struct parser* ctype_header;

    /* ¿hemos detectado si el field-name que estamos procesando refiere
     * a Content-Type?. Utilizando dentro msg para los field-name.
     */
    bool           *msg_content_type_field_detected;
};

static bool T = true;
static bool F = false;

/* Detecta si un header-field-name equivale a Content-Type.
 * Deja el valor en `ctx->msg_content_type_field_detected'. Tres valores
 * posibles: NULL (no tenemos información suficiente todavia, por ejemplo
 * viene diciendo Conten), true si matchea, false si no matchea.
 */
static void
content_type_header(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->ctype_header, c);
    do {
        debug("2.typehr", parser_utils_strcmpi_event, e);
        switch(e->type) {
            case STRING_CMP_EQ:
                ctx->msg_content_type_field_detected = &T;
                break;
            case STRING_CMP_NEQ:
                ctx->msg_content_type_field_detected = &F;
                break;
        }
        e = e->next;
    } while (e != NULL);
}

/**
 * Procesa un mensaje `tipo-rfc822'.
 * Si reconoce un al field-header-name Content-Type lo interpreta.
 *
 */
static void
mime_msg(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->msg, c);

    do {
        debug("1.   msg", mime_msg_event, e);
        switch(e->type) {
            case MIME_MSG_NAME:
                if( ctx->msg_content_type_field_detected == 0
                || *ctx->msg_content_type_field_detected) {
                    for(int i = 0; i < e->n; i++) {
                        content_type_header(ctx, e->data[i]);
                    }
                }
                break;
            case MIME_MSG_NAME_END:
                // lo dejamos listo para el próximo header
                parser_reset(ctx->ctype_header);
                ctx->msg_content_type_field_detected = NULL;
                break;
            case MIME_MSG_VALUE:
                if(ctx->msg_content_type_field_detected != 0
                && ctx->msg_content_type_field_detected) {
                    for(int i = 0; i < e->n; i++) {
                        // TODO (juan) content_type_value(ctx, e->data[i]);
                    }
                }
                break;
            case MIME_MSG_VALUE_END:
                // si parseabamos Content-Type ya terminamos
                ctx->msg_content_type_field_detected = 0;
                break;
        }
        e = e->next;
    } while (e != NULL);
}

/* Delimita una respuesta multi-línea POP3. Se encarga del "byte-stuffing" */
static void
pop3_multi(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->multi, c);
    do {
        debug("0. multi", pop3_multi_event,  e);
        switch (e->type) {
            case POP3_MULTI_BYTE:
                for(int i = 0; i < e->n; i++) {
                    mime_msg(ctx, e->data[i]);
                }
                break;
            case POP3_MULTI_WAIT:
                // nada para hacer mas que esperar
                break;
            case POP3_MULTI_FIN:
                // arrancamos de vuelta
                parser_reset(ctx->msg);
                ctx->msg_content_type_field_detected = NULL;
                break;
        }
        e = e->next;
    } while (e != NULL);
}

int
main(const int argc, const char **argv) {
    int fd = STDIN_FILENO;
    if(argc > 1) {
        fd = open(argv[1], 0);
        if(fd == -1) {
            perror("opening file");
            return 1;
        }
    }

    const unsigned int* no_class = parser_no_classes();
    struct parser_definition media_header_def =
            parser_utils_strcmpi("content-type");

    struct ctx ctx = {
        .multi        = parser_init(no_class, pop3_multi_parser()),
        .msg          = parser_init(init_char_class(), mime_message_parser()),
        .ctype_header = parser_init(no_class, &media_header_def),
    };

    uint8_t data[4096];
    int n;
    do {
        n = read(fd, data, sizeof(data));
        for(ssize_t i = 0; i < n ; i++) {
            pop3_multi(&ctx, data[i]);
        }
    } while(n > 0);

    parser_destroy(ctx.multi);
    parser_destroy(ctx.msg);
    parser_destroy(ctx.ctype_header);
    parser_utils_strcmpi_destroy(&media_header_def);
}
