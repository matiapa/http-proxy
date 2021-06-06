#include<stdio.h>
#include <stdlib.h>  // malloc
#include <string.h>  // memset


#include <http_parser.h>
#include <mime_chars.h>
#include <parser.h>
#include <logger.h>
#include <parser_utils.h>



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
//////////////////////////////////////////////////////////////////////////////
// Functions
struct  parserData * http_request_parser_init(){
    parserData * data = malloc(sizeof(*data));
    if(data != NULL){
        data->valN = 0;
        data->parser = parser_init(init_char_class(), &definition);
        data->currentMethod = calloc(METHOD_LENGTH, sizeof(char));
        data->currentTarget = calloc(TARGET_LENGTH, sizeof(char));
        data->currentHeader = calloc(HEADER_NAME_LENGTH, sizeof(char));
        data->currentHeaderValue = calloc(HEADER_NAME_VAL_LENGTH, sizeof(char));
        //data->header = malloc(sizeof(char *) * HEADER_COLUMNS );
        data->headers = malloc(sizeof (&(data->header)) * MAXHEADERS);
    }
    return data;
    /*parserData  data = {
            .valN = 0,
            .parser = parser_init(init_char_class(), &definition),
            .currentMethod = calloc(METHOD_LENGTH, sizeof(char)),
            .currentTarget = calloc(TARGET_LENGTH, sizeof(char)),
            .currentHeader = calloc(HEADER_NAME_LENGTH, sizeof(char)),
            .currentHeaderValue = calloc(HEADER_NAME_VAL_LENGTH, sizeof(char)),
            //.header = malloc(sizeof(char *) * HEADER_COLUMNS ),
            .headers = malloc(sizeof (&(data.header)) * MAXHEADERS),
    };
    return data;*/
}

void destroy_parser(parserData * data){
    for(int j = 0; j < MAXHEADERS; j++){
        for (int k = 0; k < HEADER_COLUMNS; k++){
            free(data->headers[j][k]);
        }
        free(data->headers[j]);
    }
    free(data->headers);
    free(data->currentMethod);
    free(data->currentTarget);
    free(data->currentHeader);
    free(data->currentHeaderValue);

    //antes de destroy tengo que estar seguro que dejaron de enviar y ya  pase al write deberia hacerlo afuera pero x ahora queda para no olvidarme
    parser_destroy(data->parser);
    free(data);
}

int equals(char * currentMethod, char * methodName) {
    const struct parser_definition d = parser_utils_strcmpi("foo");

    struct parser *parser = parser_init(parser_no_classes(), &d);
    for (int i = 0; i < METHOD_LENGTH && currentMethod[i] == '\0'; ++i) {
        const struct parser_event * e = parser_feed(parser, currentMethod[i]);
        do {
            switch(e->type) {
                case STRING_CMP_MAYEQ:
                    break;
                case STRING_CMP_EQ:
                    return 1;
                    break;
                case STRING_CMP_NEQ:
                    return 0;
                    break;
            }
            e = e->next;
        } while (e != NULL);
    }
}

int get_method(char * method){

    if(equals(method, "GET"))
        return GET;
    if(equals(method, "POST"))
        return POST;
    if(equals(method, "CONNECT"))
        return CONNECT;

    return OTHER;
}

void parse_http_request(uint8_t * readBuffer,struct request *httpRequest, parserData * data ,size_t readBytes) {

    /*int valN = 0;
    struct parser * p = parser_init(init_char_class(), &definition);
    char * currentMethod = calloc(METHOD_LENGTH, sizeof(char));
    char * currentTarget = calloc(TARGET_LENGTH, sizeof(char));
    char * currentHeader = calloc(HEADER_NAME_LENGTH, sizeof(char));
    char * currentHeaderValue = calloc(HEADER_NAME_VAL_LENGTH, sizeof(char));
    char ** header ;
    char *** headers = malloc(sizeof (&(header)) * MAXHEADERS);
    httpRequest->header_count = 0;*/
    size_t i = 0;

    while( i < readBytes ){
        const struct parser_event* e = parser_feed(data->parser, readBuffer[i]);
        do {
            switch(e->type) {
                case METHOD_NAME:
                    log(DEBUG, "METHOD_NAME %c",e->data[0]  );
                    data->currentMethod[data->valN++] = e->data[0];
                    break;
                case METHOD_NAME_END:
                    log(DEBUG, "METHOD_NAME_END %c",e->data[0]  );
                    data->valN = 0;
                    httpRequest->method = get_method(data->currentMethod);
                    break;
                case  TARGET_VAL:
                    log(DEBUG, "TARGET_VAL %c",e->data[0]  );
                    data->currentTarget[data->valN++] = e->data[0];

                    break;
                case  TARGET_VAL_END:
                    log(DEBUG, "TARGET_VAL %c",e->data[0]  );
                    httpRequest->url = data->currentTarget;
                    data->valN = 0;
                    break;
                case VERSION_VAL:
                    log(DEBUG, "VERSION_VAL %c",e->data[0]  );
                    break;
                case HEADER_NAME_VAL:
                    log(DEBUG, "HEADER_NAME_VAL %c",e->data[0] );
                    data->currentHeader[data->valN++] = e->data[0];
                    break;
                case HEADER_NAME_VAL_END:
                    data->header = malloc(sizeof(char *) * HEADER_COLUMNS );
                    data->header [0] = calloc(HEADER_NAME_LENGTH, sizeof(char));
                    strncpy(data->header[0], data->currentHeader, HEADER_NAME_LENGTH);
                    data->valN = 0;
                    break;
                case HEADER_VAL:
                log(DEBUG, "HEADER_VAL %c",e->data[0]  );
                    data->currentHeaderValue[data->valN++] = e->data[0];
                    break;
                case HEADER_VAL_END:
                    log(DEBUG, "HEADER_VAL_END %c",e->data[0]  );
                    data->header [1] = calloc(HEADER_NAME_VAL_LENGTH, sizeof(char));
                    strncpy(data->header[1], data->currentHeaderValue, HEADER_NAME_VAL_LENGTH);

                    data->headers [httpRequest->header_count++] = data->header;

                    memset(data->currentHeaderValue, 0 , sizeof(data->currentHeader));
                    memset(data->currentHeader, 0, sizeof(data->currentHeader));
                    log(DEBUG,"headers name: %s", data->headers[httpRequest->header_count-1][0]);
                    log(DEBUG,"headers value: %s",data->headers[httpRequest->header_count-1][1]);
                    data->valN = 0;
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
}
