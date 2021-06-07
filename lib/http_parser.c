#include<stdio.h>
#include <stdlib.h>  // malloc
#include <string.h>  // memset
#include <ctype.h> //toUpper


#include <http_parser.h>
#include <http_chars.h>
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
    END_CR,
    END,
    BODY,
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
    BODY_START,
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
static void body_start(struct parser_event *ret, const uint8_t c) {
    ret->type    = BODY_START;
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
        {.when = TOKEN_SPECIAL,         .dest = UNEXPECTED,                   .act1 = error,},
        {.when = TOKEN_DIGIT,           .dest = UNEXPECTED,                   .act1 = error,},
        {.when = ' ',                   .dest = TARGET,                       .act1 = method_end,},
};
static const struct parser_state_transition ST_TARGET [] =  {
        {.when = TOKEN_ALPHA,           .dest = TARGET,                       .act1 = target,},
        {.when = TOKEN_SPECIAL,         .dest = TARGET,                       .act1 = target,},
        {.when = TOKEN_DIGIT,           .dest = TARGET,                       .act1 = target,},
        {.when = ' ',                   .dest = VERSION,                      .act1 = target_end,},
};
static const struct parser_state_transition ST_VERSION [] =  {
        {.when = TOKEN_ALPHA,           .dest = VERSION,                      .act1 = version,},
        {.when = TOKEN_SPECIAL,         .dest = VERSION,                      .act1 = version,},
        {.when = TOKEN_DIGIT,           .dest = VERSION,                      .act1 = version,},
        {.when = '\r',                  .dest = HEADER_CR,                    .act1 = wait,},
};
static const struct parser_state_transition ST_HEADER_CR[] =  {
        {.when = '\n',                  .dest = CHECK_IF_LAST_HEADER,         .act1 = wait,},
        {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_HEADER_NAME [] =  {
        {.when = TOKEN_ALPHA,           .dest = HEADER_NAME,                  .act1 = header_name,},
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
        {.when = '\r',                  .dest = END_CR,                       .act1 = wait,},
        {.when = TOKEN_ALPHA,           .dest = HEADER_NAME,                  .act1 = header_name,},
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
static const struct parser_state_transition ST_END_CR [] =  {
        {.when = '\n',                  .dest = END,                          .act1 = end,},
        {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};
static const struct parser_state_transition ST_END [] =  {
        {.when = ANY,                   .dest = BODY,                         .act1 = body_start,},
};
static const struct parser_state_transition ST_BODY [] =  {
        {.when = ANY,                   .dest = BODY,                         .act1 = wait,},
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
        ST_END_CR,
        ST_END,
        ST_BODY,
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
        N(ST_END_CR),
        N(ST_END),
        N(ST_BODY),
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
        data->headerCount = 0;
    }
    return data;
}

void http_parser_reset(parserData * data){
    data->valN = 0;
    parser_reset(data->parser);
    memset(data->currentMethod, 0, sizeof(data->currentMethod));
    memset(data->currentTarget, 0, sizeof(data->currentTarget));
    memset(data->currentHeader, 0, sizeof(data->currentHeader));
    memset(data->currentHeaderValue, 0, sizeof(data->currentHeaderValue));
    memset(data->headers, 0, sizeof(data->headers));
    data->headerCount = 0;
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

    //antes de destroy tengo que estar seguro que dejaron de enviar y ya  pase al write
    parser_destroy(data->parser);
    free(data);
}



methods get_method(char * method){

    if(strcmp(method, "GET") == 0)
        return GET;
    if(strcmp(method, "POST") == 0)
        return POST;
    if(strcmp(method, "CONNECT") == 0)
        return CONNECT;

    return OTHER;
}

parse_state parse_http_request(uint8_t * readBuffer,struct request *httpRequest, parserData * data ,size_t readBytes) {

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
                    data->currentMethod[data->valN++] = toupper(e->data[0]);
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

                    data->headers [data->headerCount++] = data->header;

                    memset(data->currentHeaderValue, 0 , sizeof(data->currentHeader));
                    memset(data->currentHeader, 0, sizeof(data->currentHeader));
                    log(DEBUG,"headers name: %s", data->headers[data->headerCount-1][0]);
                    log(DEBUG,"headers value: %s",data->headers[data->headerCount-1][1]);
                    data->valN = 0;
                    break;
                case WAIT_MSG:
                log(DEBUG, "WAIT_MSG %c",e->data[0]  );
                    break;
                case UNEXPECTED_VALUE:
                log(DEBUG, "UNEXPECTED_VALUE %c",e->data[0]  );
                    http_parser_reset(data);
                    return FAILED;
                    break;
                case BODY_START:
                log(DEBUG, "BODY_START %c",e->data[0]  );
                    break;
                case FINAL_VAL:
                log(DEBUG, "FINAL_VAL %c",e->data[0]  );
                    http_parser_reset(data);
                    httpRequest->headers = data->headers;
                    httpRequest->header_count = data->headerCount;
                    return SUCCESS;
                    break;
            }
            e = e->next;
        } while (e != NULL);
        i++;
    }
    return PENDING;
}
