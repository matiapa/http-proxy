#include "parser.h"
#include "mime_chars.h"
#include "mime_msg.h"

/**
 * RFC822:
 *
 * field       =  field-name ":" [ field-body ] CRLF
 *
 * field-name  =  1*<any CHAR, excluding CTLs, SPACE, and ":">
 *
 * field-body  =  field-body-contents
 *               [CRLF LWSP-char field-body]
 *
 * field-body-contents =
 *              <the ASCII characters making up the field-body, as
 *               defined in the following sections, and consisting
 *               of combinations of atom, quoted-string, and
 *               specials tokens, or else consisting of texts>
 *
 * linear-white-space =  1*([CRLF] LWSP-char)  ; semantics = SPACE
 *                                             ; CRLF => folding
 */
enum state {
    NAME0,
    NAME,
    VALUE,
    VALUE_CR,
    VALUE_CRLF,
    FOLD,
    VALUE_CRLF_CR,
    BODY,
    ERROR,
};

///////////////////////////////////////////////////////////////////////////////
// Acciones

static void
name(struct parser_event *ret, const uint8_t c) {
    ret->type    = MIME_MSG_NAME;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
name_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = MIME_MSG_NAME_END;
    ret->n       = 1;
    ret->data[0] = ':';
}

static void
value(struct parser_event *ret, const uint8_t c) {
    ret->type    = MIME_MSG_VALUE;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
value_cr(struct parser_event *ret, const uint8_t c) {
    ret->type    = MIME_MSG_VALUE;
    ret->n       = 1;
    ret->data[0] = '\r';
}

static void
value_fold_crlf(struct parser_event *ret, const uint8_t c) {
    ret->type    = MIME_MSG_VALUE_FOLD;
    ret->n       = 2;
    ret->data[0] = '\r';
    ret->data[1] = '\n';
}

static void
value_fold(struct parser_event *ret, const uint8_t c) {
    ret->type    = MIME_MSG_VALUE_FOLD;
    ret->n       = 1;
    ret->data[0] = c ;
}

static void
value_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = MIME_MSG_VALUE_END;
    ret->n       = 2;
    ret->data[0] = '\r';
    ret->data[1] = '\n';
}

static void
wait(struct parser_event *ret, const uint8_t c) {
    ret->type    = MIME_MSG_WAIT;
    ret->n       = 0;
}

static void
body_start(struct parser_event *ret, const uint8_t c) {
    ret->type    = MIME_MSG_BODY_START;
    ret->n       = 2;
    ret->data[0] = '\r';
    ret->data[1] = '\n';
}

static void
body(struct parser_event *ret, const uint8_t c) {
    ret->type    = MIME_MSG_BODY;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
unexpected(struct parser_event *ret, const uint8_t c) {
    ret->type    = MIME_MSG_UNEXPECTED;
    ret->n       = 1;
    ret->data[0] = c;
}

///////////////////////////////////////////////////////////////////////////////
// Transiciones

static const struct parser_state_transition ST_NAME0[] =  {
    {.when = ':',        .dest = ERROR,         .act1 = unexpected,},
    {.when = ' ',        .dest = ERROR,         .act1 = unexpected,},
    {.when = TOKEN_CTL,  .dest = ERROR,         .act1 = unexpected,},
    {.when = TOKEN_CHAR, .dest = NAME,          .act1 = name,      },
    {.when = ANY,        .dest = ERROR,         .act1 = unexpected,},
};

static const struct parser_state_transition ST_NAME[] =  {
    {.when = ':',        .dest = VALUE,         .act1 = name_end,  },
    {.when = ' ',        .dest = ERROR,         .act1 = unexpected,},
    {.when = TOKEN_CTL,  .dest = ERROR,         .act1 = unexpected,},
    {.when = TOKEN_CHAR, .dest = NAME,          .act1 = name,      },
    {.when = ANY,        .dest = ERROR,         .act1 = unexpected,},
};

static const struct parser_state_transition ST_VALUE[] =  {
    {.when = '\r',       .dest = VALUE_CR,       .act1 = wait,      },
    {.when = ANY,        .dest = VALUE,          .act1 = value,     },
};

static const struct parser_state_transition ST_VALUE_CR[] =  {
    {.when = '\n',       .dest = VALUE_CRLF,     .act1 = wait,      },
    {.when = ANY,        .dest = VALUE,          .act1 = value_cr,
                                                 .act2 = value,     },
};

static const struct parser_state_transition ST_VALUE_CRLF[] =  {
    {.when = ':',        .dest = ERROR,          .act1 = unexpected,},
    {.when = '\r',       .dest = VALUE_CRLF_CR,  .act1 = wait,},
    {.when = TOKEN_LWSP, .dest = FOLD,           .act1 = value_fold_crlf,
                                                 .act2 = value_fold,},
    {.when = TOKEN_CTL,  .dest = ERROR,          .act1 = value_end,
                                                 .act2 = unexpected,},
    {.when = TOKEN_CHAR, .dest = NAME,           .act1 = value_end,
                                                 .act2 = name,      },
    {.when = ANY,        .dest = ERROR,          .act1 = unexpected,},
};

static const struct parser_state_transition ST_FOLD[] =  {
    {.when =TOKEN_LWSP,  .dest = FOLD,           .act1 = value_fold,},
    {.when = ANY,        .dest = VALUE,          .act1 = value,     },
};

static const struct parser_state_transition ST_VALUE_CRLF_CR[] =  {
    {.when = '\n',        .dest = BODY,          .act1 = value_end,
                                                 .act2 = body_start,},
    {.when = ANY,         .dest = ERROR,         .act1 = value_end,
                                                 .act2 = unexpected,},
};

static const struct parser_state_transition ST_BODY[] =  {
    {.when = ANY,        .dest = BODY,           .act1 = body,},
};

static const struct parser_state_transition ST_ERROR[] =  {
    {.when = ANY,        .dest = ERROR,           .act1 = unexpected,},
};

///////////////////////////////////////////////////////////////////////////////
// Declaración formal

static const struct parser_state_transition *states [] = {
    ST_NAME0,
    ST_NAME,
    ST_VALUE,
    ST_VALUE_CR,
    ST_VALUE_CRLF,
    ST_FOLD,
    ST_VALUE_CRLF_CR,
    ST_BODY,
    ST_ERROR,
};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static const size_t states_n [] = {
    N(ST_NAME0),
    N(ST_NAME),
    N(ST_VALUE),
    N(ST_VALUE_CR),
    N(ST_VALUE_CRLF),
    N(ST_FOLD),
    N(ST_VALUE_CRLF_CR),
    N(ST_BODY),
    N(ST_ERROR),
};

static struct parser_definition definition = {
    .states_count = N(states),
    .states       = states,
    .states_n     = states_n,
    .start_state  = NAME0,
};

const struct parser_definition * 
mime_message_parser(void) {
    return &definition;
}

const char *
mime_msg_event(enum mime_msg_event_type type) {
    const char *ret;

    switch(type) {
        case MIME_MSG_NAME:
          ret = "name(c)";
          break;
        case MIME_MSG_NAME_END:
            ret = "name_end(':')";
            break;
        case MIME_MSG_VALUE:
            ret = "value(c)";
            break;
        case MIME_MSG_VALUE_FOLD:
            ret = "value_fold(c)";
            break;
        case MIME_MSG_VALUE_END:
            ret = "value_end(CRLF)";
            break;
        case MIME_MSG_BODY_START:
            ret = "start_body(c)";
            break;
        case MIME_MSG_BODY:
            ret = "body(c)";
            break;
        case MIME_MSG_WAIT:
            ret = "wait()";
            break;
        case MIME_MSG_UNEXPECTED:
            ret = "unexepected(c)";
            break;
    }
    return ret;
}

