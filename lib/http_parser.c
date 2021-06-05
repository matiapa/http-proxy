#include<stdio.h>
#include <stdlib.h>  // malloc
#include <string.h>  // memset
#include <assert.h>  // assert
#include <errno.h>
#include <time.h>
#include <unistd.h>  // close
#include <pthread.h>

#include <http_parser.h>
#include <mime_chars.h>
#include <parser.h>
#include <logger.h>




enum states {
    METHOD,
    TARGET,
    VERSION,
    HEADER_CR,
    HEADER_NAME,
    HEADER_VALUE,
    CHECK_IF_LAST_HEADER,
    IGNORE,
    UNEXPECTED,
    END,
};
enum event_type{
    METHOD_NAME,
    METHOD_NAME_END,
    TARGET_VAL,
    TARGET_VAL_END,
    VERSION_VAL,
    HEADER_NAME_VAL,
    HEADER_VAL,
    WAIT_MSG,
    UNEXPECTED_VALUE,
    FINAL_VAL,
};


///////////////////////////////////////////////////////////////////////////////
// Acciones
static void method(struct parser_event *ret, const uint8_t c) {
    ret->type    = METHOD_NAME;
    ret->n       = 1;
    ret->data[0] = c;
}

static void error(struct parser_event *ret, const uint8_t c) {
    ret->type    = UNEXPECTED_VALUE;
    ret->n       = 1;
    ret->data[0] = c;
}
static void method_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = METHOD_NAME_END;
    ret->n       = 1;
    ret->data[0] = c;
}
static void target(struct parser_event *ret, const uint8_t c) {
    ret->type    = TARGET_VAL;
    ret->n       = 1;
    ret->data[0] = c;
}
static void target_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = TARGET_VAL_END;
    ret->n       = 1;
    ret->data[0] = c;
}
static void version(struct parser_event *ret, const uint8_t c) {
    ret->type    = VERSION_VAL;
    ret->n       = 1;
    ret->data[0] = c;
}
static void wait(struct parser_event *ret, const uint8_t c) {
    ret->type    = WAIT_MSG;
    ret->n       = 1;
    ret->data[0] = c;
}
static void header_name(struct parser_event *ret, const uint8_t c) {
    ret->type    = HEADER_NAME_VAL;
    ret->n       = 1;
    ret->data[0] = c;
}
static void header_value(struct parser_event *ret, const uint8_t c) {
    ret->type    = HEADER_VAL;
    ret->n       = 1;
    ret->data[0] = c;
}
static void end(struct parser_event *ret, const uint8_t c) {
    ret->type    = FINAL_VAL;
    ret->n       = 1;
    ret->data[0] = c;
}
///////////////////////////////////////////////////////////////////////////////
// Transiciones

static const struct parser_state_transition ST_METHOD [] =  {
        {.when = TOKEN_ALPHA,           .dest = METHOD,         .act1 = method,},
        {.when = TOKEN_SPECIAL,         .dest = UNEXPECTED,          .act1 = error,},//casi seguro que especials son los ascci de especials
        {.when = TOKEN_DIGIT,           .dest = UNEXPECTED,          .act1 = error,},
        {.when = ' ',                   .dest = TARGET,         .act1 = method_end,},
};
static const struct parser_state_transition ST_TARGET [] =  {
        {.when = TOKEN_ALPHA,           .dest = TARGET,         .act1 = target,},
        {.when = TOKEN_SPECIAL,         .dest = TARGET,          .act1 = target,},//casi seguro que especials son los ascci de especials
        {.when = TOKEN_DIGIT,           .dest = TARGET,          .act1 = target,},
        {.when = ' ',                   .dest = VERSION,        .act1 = target_end,},
};
static const struct parser_state_transition ST_VERSION [] =  {
        {.when = TOKEN_ALPHA,           .dest = VERSION,         .act1 = version,},
        {.when = TOKEN_SPECIAL,         .dest = VERSION,          .act1 = version,},//casi seguro que especials son los ascci de especials
        {.when = TOKEN_DIGIT,           .dest = VERSION,          .act1 = version,},
        {.when = '\r',                   .dest = HEADER_CR,        .act1 = wait,},
};
static const struct parser_state_transition ST_HEADER_CR[] =  {
        {.when = '\n',       .dest = CHECK_IF_LAST_HEADER,     .act1 = wait,},
        {.when = ANY,        .dest = ERROR,          .act1 = error,},
};

static const struct parser_state_transition ST_HEADER_NAME [] =  {
        {.when = TOKEN_ALPHA,        .dest =HEADER_NAME,        .act1 = header_name,},
        {.when = TOKEN_LWSP,        .dest = IGNORE,        .act1 = wait,},
        {.when = ':',        .dest = HEADER_VALUE,        .act1 = header_value,},
};
static const struct parser_state_transition ST_HEADER_VALUE [] =  {
        {.when = TOKEN_ALPHA,        .dest = HEADER_VALUE,          .act1 = header_value,},
        {.when = TOKEN_DIGIT,        .dest = HEADER_VALUE,          .act1 = header_value,},
        {.when = TOKEN_SPECIAL,        .dest = HEADER_VALUE,        .act1 = header_value,},
        {.when = '\r',        .dest = HEADER_CR,         .act1 = wait,},
};
static const struct parser_state_transition ST_CHECK_IF_LAST_HEADER [] =  {
        {.when = '\r',        .dest = END,                          .act1 = end,},
        {.when = TOKEN_ALPHA,        .dest =HEADER_NAME,        .act1 = header_name,},
        {.when = TOKEN_LWSP,        .dest = IGNORE,        .act1 = wait,},
        {.when = ANY,        .dest = UNEXPECTED,                  .act1 = error,},
};
static const struct parser_state_transition ST_IGNORE [] =  {
        {.when = ' ',        .dest = IGNORE,        .act1 = wait,},
        {.when = TOKEN_ALPHA,        .dest = HEADER_NAME,        .act1 = header_name,},
        {.when = ANY,        .dest = UNEXPECTED,        .act1 = error,},
};
static const struct parser_state_transition ST_END [] =  {
        {.when = ANY,        .dest = UNEXPECTED,        .act1 = error,},
};

///////////////////////////////////////////////////////////////////////////////
// Declaración formal

static const struct parser_state_transition *states [] = {
        ST_METHOD,
        ST_TARGET,
        ST_VERSION,
        ST_HEADER_CR,
        ST_HEADER_NAME,
        ST_HEADER_VALUE,
        ST_CHECK_IF_LAST_HEADER,
        ST_IGNORE,
        ST_END,
};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static const size_t states_n [] = {
        N(ST_METHOD),
        N(ST_TARGET),
        N(ST_VERSION),
        N(ST_HEADER_CR),
        N(ST_HEADER_NAME),
        N(ST_HEADER_VALUE),
        N(ST_CHECK_IF_LAST_HEADER),
        N(ST_IGNORE),
        N(ST_END),
};

static struct parser_definition definition = {
        .states_count = N(states),
        .states       = states,
        .states_n     = states_n,
        .start_state  = METHOD,
};

void parse_http_request(uint8_t * readBuffer, struct request *httpRequest) {

    int i = 0;
    log(DEBUG, "entroooooooooooo");
    struct parser *parser = parser_init(init_char_class(), &definition);

    while( readBuffer[i]!= 0 ){
        const struct parser_event* e = parser_feed(parser, readBuffer[i]);
        do {
            switch(e->type) {
                case METHOD_NAME:
                    log(DEBUG, "METHOD_NAME %s",e->data );
                    //for(int i = 0; i < e->n; i++) {
                        //check_method(e->data[i]);//paso letra por letra el metodo deberia ver que metodo es y actuar acorde
                    //}
                    break;
                case METHOD_NAME_END:
                    log(DEBUG, "METHOD_NAME_END %s",e->data );

                    //manejar los : y preparar siguiente
                    break;
                case  TARGET_VAL:
                    log(DEBUG, "TARGET_VAL %s",e->data );
                    break;
                case  TARGET_VAL_END:
                log(DEBUG, "TARGET_VAL %s",e->data );
                    break;
                case VERSION_VAL:
                log(DEBUG, "VERSION_VAL %s",e->data );

                    break;
                case HEADER_NAME_VAL:
                log(DEBUG, "HEADER_NAME_VAL %s",e->data );
                    break;
                case HEADER_VAL:
                log(DEBUG, "HEADER_VAL %s",e->data );
                    break;
                case WAIT_MSG:
                log(DEBUG, "WAIT_MSG %s",e->data );
                    break;
                case UNEXPECTED_VALUE:
                log(DEBUG, "UNEXPECTED_VALUE %s",e->data );
                    break;
                case FINAL_VAL:
                log(DEBUG, "FINAL_VAL %s",e->data );
                    break;
            }
            e = e->next;
        } while (e != NULL);
        i++;
    }

    //antes de destroy tengo que estar seguro que dejaron de enviar y ya  pase al write deberia hacerlo afuera pero x ahora queda para no olvidarme
    parser_destroy(parser);

}
