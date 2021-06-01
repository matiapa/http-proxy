#include "pop3_multi.h"

const char *
pop3_multi_event(enum pop3_multi_type type) {
    const char *ret;
    switch(type) {
        case POP3_MULTI_BYTE:
            ret = "byte(c)";
            break;
        case POP3_MULTI_WAIT:
            ret = "wait()";
            break;
        case POP3_MULTI_FIN:
            ret = "fin()";
            break;
    }
    return ret;
}

enum {
    NEWLINE,
    BYTE,
    CR,
    DOT,
    DOT_CR,
    FIN,
};

static void
byte(struct parser_event *ret, const uint8_t c) {
    ret->type    = POP3_MULTI_BYTE;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
byte_cr(struct parser_event *ret, const uint8_t c) {
    byte(ret, '\r');
}

static void
wait(struct parser_event *ret, const uint8_t c) {
    ret->type    = POP3_MULTI_WAIT;
    ret->n       = 0;
}

static void
fin(struct parser_event *ret, const uint8_t c) {
    ret->type    = POP3_MULTI_FIN;
    ret->n       = 0;
}

static const struct parser_state_transition ST_NEWLINE[] =  {
    {.when = '.',        .dest = DOT,         .act1 = wait,},
    {.when = ANY,        .dest = BYTE,        .act1 = byte,},
};

static const struct parser_state_transition ST_BYTE[] =  {
    {.when = '\r',       .dest = CR,          .act1 = wait,},
    {.when = ANY,        .dest = BYTE,        .act1 = byte,},
};

static const struct parser_state_transition ST_CR[] =  {
    {.when = '\n',       .dest = NEWLINE,     .act1 = byte_cr, .act2 = byte},
    {.when = ANY,        .dest = BYTE,        .act1 = byte_cr, .act2 = byte},
};

static const struct parser_state_transition ST_DOT[] =  {
    {.when = '\r',       .dest = DOT_CR,      .act1 = wait},
    {.when = ANY,        .dest = BYTE,        .act1 = byte},
};

static const struct parser_state_transition ST_DOT_CR[] =  {
    {.when = '\n',       .dest = FIN,         .act1 = fin},
    {.when = ANY,        .dest = BYTE,        .act1 = byte_cr, .act2 = byte},
};

static const struct parser_state_transition ST_FIN[] =  {
    {.when = ANY,        .dest = BYTE,        .act1 = byte_cr, .act2 = byte},
};

///////////////////////////////////////////////////////////////////////////////
// Declaración formal

static const struct parser_state_transition *states [] = {
    ST_NEWLINE,
    ST_BYTE,
    ST_CR,
    ST_DOT,
    ST_DOT_CR,
    ST_FIN,
};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static const size_t states_n [] = {
    N(ST_NEWLINE),
    N(ST_BYTE),
    N(ST_CR),
    N(ST_DOT),
    N(ST_DOT_CR),
    N(ST_FIN),
};

static struct parser_definition definition = {
    .states_count = N(states),
    .states       = states,
    .states_n     = states_n,
    .start_state  = NEWLINE,
};

const struct parser_definition *
pop3_multi_parser(void) {
    return &definition;
}
