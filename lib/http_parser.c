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
    HEADER_NAME_VAL_END,
    HEADER_VAL,
    HEADER_VAL_END,
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
static void header_name_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = HEADER_NAME_VAL_END;
    ret->n       = 1;
    ret->data[0] = c;
}
static void header_value(struct parser_event *ret, const uint8_t c) {
    ret->type    = HEADER_VAL;
    ret->n       = 1;
    ret->data[0] = c;
}
static void header_value_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = HEADER_VAL_END;
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
        {.when = TOKEN_ALPHA,           .dest = METHOD,                       .act1 = method,},
        {.when = TOKEN_SPECIAL,         .dest = UNEXPECTED,                   .act1 = error,},//casi seguro que especials son los ascci de especials
        {.when = TOKEN_DIGIT,           .dest = UNEXPECTED,                   .act1 = error,},
        {.when = ' ',                   .dest = TARGET,                       .act1 = method_end,},
};
static const struct parser_state_transition ST_TARGET [] =  {
        {.when = TOKEN_ALPHA,           .dest = TARGET,                       .act1 = target,},
        {.when = TOKEN_SPECIAL,         .dest = TARGET,                       .act1 = target,},//casi seguro que especials son los ascci de especials
        {.when = TOKEN_DIGIT,           .dest = TARGET,                       .act1 = target,},
        {.when = ' ',                   .dest = VERSION,                      .act1 = target_end,},
};
static const struct parser_state_transition ST_VERSION [] =  {
        {.when = TOKEN_ALPHA,           .dest = VERSION,                      .act1 = version,},
        {.when = TOKEN_SPECIAL,         .dest = VERSION,                      .act1 = version,},//casi seguro que especials son los ascci de especials
        {.when = TOKEN_DIGIT,           .dest = VERSION,                      .act1 = version,},
        {.when = '\r',                  .dest = HEADER_CR,                    .act1 = wait,},
};
static const struct parser_state_transition ST_HEADER_CR[] =  {
        {.when = '\n',                  .dest = CHECK_IF_LAST_HEADER,         .act1 = wait,},
        {.when = ANY,                   .dest = ERROR,                        .act1 = error,},
};

static const struct parser_state_transition ST_HEADER_NAME [] =  {
        {.when = TOKEN_ALPHA,           .dest =HEADER_NAME,                   .act1 = header_name,},
        {.when = TOKEN_LWSP,            .dest = IGNORE,                       .act1 = wait,},
        {.when = ':',                   .dest = HEADER_VALUE,                 .act1 = header_name_end,},
};
static const struct parser_state_transition ST_HEADER_VALUE [] =  {
        {.when = TOKEN_ALPHA,           .dest = HEADER_VALUE,                 .act1 = header_value,},
        {.when = TOKEN_DIGIT,           .dest = HEADER_VALUE,                 .act1 = header_value,},
        {.when = TOKEN_SPECIAL,         .dest = HEADER_VALUE,                 .act1 = header_value,},
        {.when = '\r',                  .dest = HEADER_CR,                    .act1 = header_value_end,},
};
static const struct parser_state_transition ST_CHECK_IF_LAST_HEADER [] =  {
        {.when = '\r',                  .dest = END,                          .act1 = end,},
        {.when = TOKEN_ALPHA,           .dest =HEADER_NAME,                   .act1 = header_name,},
        {.when = TOKEN_LWSP,            .dest = IGNORE,                       .act1 = wait,},
        {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};
static const struct parser_state_transition ST_IGNORE [] =  {
        {.when = ' ',                   .dest = IGNORE,                       .act1 = wait,},
        {.when = TOKEN_ALPHA,           .dest = HEADER_NAME,                  .act1 = header_name,},
        {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};
static const struct parser_state_transition ST_UNEXPECTED [] =  {
        {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};
static const struct parser_state_transition ST_END [] =  {
        {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
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
        ST_UNEXPECTED,
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
        N(ST_UNEXPECTED),
        N(ST_END),
};

static struct parser_definition definition = {
        .states_count = N(states),
        .states       = states,
        .states_n     = states_n,
        .start_state  = METHOD,
};

struct  parserData * http_request_parser_init(){


    int valN = 0;
    struct parser *parser = parser_init(init_char_class(), &definition);
    char currentMethod [METHOD_LENGTH] = {'\0'};
    char currentTarget [TARGET_LENGTH] = {'\0'};
    char * header[HEADER_COLUMNS];
    //char *** headers = malloc((sizeof (char *) * HEADER_COLUMNS) * MAXHEADERS);
    char headers [MAXHEADERS][2][HEADER_NAME_VAL_LENGTH];
    char currentHeader [HEADER_NAME_LENGTH] = {'\0'};
    char currentHeaderValue [HEADER_NAME_VAL_LENGTH] = {'\0'};
}

void parse_http_request(uint8_t * readBuffer, struct request *httpRequest, struct parser * parser, size_t readBytes) {

    size_t i = 0;

    while( i < readBytes ){
        const struct parser_event* e = parser_feed(parser, readBuffer[i]);
        do {
            switch(e->type) {
                case METHOD_NAME:
                    log(DEBUG, "METHOD_NAME %c",e->data[0]  );
                    currentMethod[i] = e->data[0];
                    break;
                case METHOD_NAME_END:
                    log(DEBUG, "METHOD_NAME_END %c",e->data[0]  );
                    //get_method(currentMethod);
                    //manejar los : y preparar siguiente
                    break;
                case  TARGET_VAL:
                    log(DEBUG, "TARGET_VAL %c",e->data[0]  );
                    currentTarget[valN++] = e->data[0];

                    break;
                case  TARGET_VAL_END:
                    log(DEBUG, "TARGET_VAL %c",e->data[0]  );
                    httpRequest->url = currentTarget;
                    valN = 0;
                    break;
                case VERSION_VAL:
                    log(DEBUG, "VERSION_VAL %c",e->data[0]  );
                    break;
                case HEADER_NAME_VAL:
                    log(DEBUG, "HEADER_NAME_VAL %c",e->data[0] );
                    currentHeader[valN++] = e->data[0];
                    break;
                case HEADER_NAME_VAL_END:
                    //header [0] = malloc(strlen(currentHeader));
                    //strncpy(header[0], currentHeader, strlen(header[0]));
                    valN = 0;
                    break;
                case HEADER_VAL:
                log(DEBUG, "HEADER_VAL %c",e->data[0]  );
                    currentHeaderValue[valN++] = e->data[0];
                    break;
                case HEADER_VAL_END:
                    log(DEBUG, "HEADER_VAL_END %c",e->data[0]  );
                    //header [1] = malloc(strlen(currentHeaderValue));
                   // strncpy(header[1], currentHeaderValue, strlen(header[1]));

                   // headers [httpRequest->header_count] [0]= malloc(HEADER_NAME_LENGTH);
                   // headers [httpRequest->header_count] [1]= malloc(HEADER_NAME_VAL_LENGTH);

                    strncpy(headers [httpRequest->header_count][0],currentHeader, HEADER_NAME_LENGTH);
                    strncpy(headers [httpRequest->header_count++][1],currentHeaderValue, HEADER_NAME_VAL_LENGTH);

                    log(DEBUG,"headers name: %s value: % s", headers[httpRequest->header_count-1][0],headers[httpRequest->header_count-1][1]);

                    //free(header[0]);
                    //free(header[1]);
                    //memset(header,0, sizeof(header));

                    memset(currentHeaderValue, 0 , sizeof(currentHeader));
                    memset(currentHeader, 0, sizeof(currentHeader));
                    log(DEBUG,"headers name: %s value: % s", headers[httpRequest->header_count-1][0],headers[httpRequest->header_count-1][1]);
                    valN = 0;
                    break;
                case WAIT_MSG:
                log(DEBUG, "WAIT_MSG %c",e->data[0]  );
                    break;
                case UNEXPECTED_VALUE:
                log(DEBUG, "UNEXPECTED_VALUE %c",e->data[0]  );
                    break;
                case FINAL_VAL:
                log(DEBUG, "FINAL_VAL %c",e->data[0]  );
                    break;
            }
            e = e->next;
        } while (e != NULL);
        i++;
    }
    //antes de destroy tengo que estar seguro que dejaron de enviar y ya  pase al write deberia hacerlo afuera pero x ahora queda para no olvidarme
    parser_destroy(parser);

}
