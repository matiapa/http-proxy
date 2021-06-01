#ifndef MIME_MSG_H_be03ad0cccde0231647a6c699e44f0d753baf4dd
#define MIME_MSG_H_be03ad0cccde0231647a6c699e44f0d753baf4dd

/**
 * mime_msg.c - tokenizador de mensajes "tipo" message/rfc822.
 *
 * "Tipo" porque simplemente detectamos partes pero no requerimos ningún
 * header en particular.
 *
 */
#include "parser.h"

/** tipo de eventos de un mensaje mime */
struct parser;
enum mime_msg_event_type {
    /* caracter del nombre de un header. payload: caracter. */
    MIME_MSG_NAME,

    /* el nombre del header está completo. payload: ':'. */
    MIME_MSG_NAME_END,

    /* caracter del value de un header. payload: caracter. */
    MIME_MSG_VALUE,

    /* se ha foldeado el valor. payload: CR LF */
    MIME_MSG_VALUE_FOLD,

    /* el valor de un header está completo. CR LF  */
    MIME_MSG_VALUE_END,

    /* comienza el body */
    MIME_MSG_BODY_START,

    /* se recibió un caracter que pertence al body */
    MIME_MSG_BODY,

    /* no tenemos idea de qué hacer hasta que venga el proximo caracter */
    MIME_MSG_WAIT,

    /* se recibió un caracter que no se esperaba */
    MIME_MSG_UNEXPECTED,
};

/** la definición del parser */
const struct parser_definition * mime_message_parser(void);

const char *
mime_msg_event(enum mime_msg_event_type type);

#endif
