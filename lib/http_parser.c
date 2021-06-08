#include<stdio.h>
#include <stdlib.h>  // malloc
#include <string.h>  // memset
#include <ctype.h> //toUpper

#include <http_parser.h>
#include <http_chars.h>
#include <parser.h>
#include <logger.h>

#define PARSE_BUFF_SIZE 1024


enum states {
    METHOD,
    TARGET,
    VERSION,

    REQ_LINE_CR,
    REQ_LINE_CRLF,

    HEADER_NAME,
    HEADER_VALUE,

    HEADER_LINE_CR,
    HEADER_LINE_CRLF,

    HEADERS_ENDLINE_CR,
    HEADERS_ENDLINE_CRLF,

    BODY,

    UNEXPECTED,
};

enum event_type{
    METHOD_NAME,
    METHOD_NAME_END,

    TARGET_VAL,
    TARGET_VAL_END,

    VERSION_VAL,
    VERSION_VAL_END,
    
    HEADER_NAME_VAL,
    HEADER_NAME_END,

    HEADER_VALUE_VAL,
    HEADER_VALUE_END,

    HEADER_SECTION_END,

    BODY_VAL,

    WAIT_MSG,
    UNEXPECTED_VALUE,
};


///////////////////////////////////////////////////////////////////////////////
// Acciones

static void method(struct parser_event *ret, const uint8_t c) {
    ret->type    = METHOD_NAME;
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

static void version_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = VERSION_VAL_END;
    ret->n       = 1;
    ret->data[0] = c;
}

static void header_name(struct parser_event *ret, const uint8_t c) {
    ret->type    = HEADER_NAME;
    ret->n       = 1;
    ret->data[0] = c;
}

static void header_name_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = HEADER_NAME_END;
    ret->n       = 1;
    ret->data[0] = c;
}

static void header_value(struct parser_event *ret, const uint8_t c) {
    ret->type    = HEADER_VALUE_VAL;
    ret->n       = 1;
    ret->data[0] = c;
}

static void header_value_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = HEADER_VALUE_END;
    ret->n       = 1;
    ret->data[0] = c;
}

static void header_section_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = HEADER_SECTION_END;
    ret->n       = 1;
    ret->data[0] = c;
}

static void body(struct parser_event *ret, const uint8_t c) {
    ret->type    = BODY_VAL;
    ret->n       = 1;
    ret->data[0] = c;
}

static void wait_msg(struct parser_event *ret, const uint8_t c) {
    ret->type    = WAIT_MSG;
    ret->n       = 1;
    ret->data[0] = c;
}

static void error(struct parser_event *ret, const uint8_t c) {
    ret->type    = UNEXPECTED_VALUE;
    ret->n       = 1;
    ret->data[0] = c;
}

///////////////////////////////////////////////////////////////////////////////
// Transiciones

static const struct parser_state_transition ST_METHOD [] =  {
    {.when = TOKEN_ALPHA,           .dest = METHOD,                       .act1 = method,},
    {.when = ' ',                   .dest = TARGET,                       .act1 = method_end,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_TARGET [] =  {
    {.when = TOKEN_ALPHA,           .dest = TARGET,                       .act1 = target,},
    {.when = TOKEN_DIGIT,           .dest = TARGET,                       .act1 = target,},
    {.when = TOKEN_SPECIAL,         .dest = TARGET,                       .act1 = target,},
    {.when = ' ',                   .dest = VERSION,                      .act1 = target_end,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_VERSION [] =  {
    {.when = TOKEN_ALPHA,           .dest = VERSION,                      .act1 = version,},
    {.when = TOKEN_DIGIT,           .dest = VERSION,                      .act1 = version,},
    {.when = TOKEN_SPECIAL,         .dest = VERSION,                      .act1 = version,},
    {.when = '\r',                  .dest = REQ_LINE_CR,                  .act1 = version_end,},
    {.when = '\n',                  .dest = REQ_LINE_CRLF,                .act1 = version_end,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_REQ_LINE_CR[] =  {
    {.when = '\n',                  .dest = REQ_LINE_CRLF,                .act1 = wait_msg,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_REQ_LINE_CRLF[] =  {
    {.when = '\r',                  .dest = HEADERS_ENDLINE_CR,           .act1 = wait_msg,},
    {.when = '\n',                  .dest = HEADERS_ENDLINE_CRLF,         .act1 = header_section_end,},
    {.when = TOKEN_ALPHA,           .dest = HEADER_NAME,                  .act1 = header_name,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_HEADER_NAME [] =  {
    {.when = TOKEN_ALPHA,           .dest = HEADER_NAME,                  .act1 = header_name,},
    {.when = '-',           .dest = HEADER_NAME,                  .act1 = header_name,},
    {.when = ':',                   .dest = HEADER_VALUE,                 .act1 = header_name_end,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_HEADER_VALUE [] =  {
    {.when = TOKEN_ALPHA,           .dest = HEADER_VALUE,                 .act1 = header_value,},
    {.when = TOKEN_DIGIT,           .dest = HEADER_VALUE,                 .act1 = header_value,},
    {.when = TOKEN_SPECIAL,         .dest = HEADER_VALUE,                 .act1 = header_value,},
    {.when = TOKEN_LWSP,            .dest = HEADER_VALUE,                 .act1 = header_value,},
    {.when = '\r',                  .dest = HEADER_LINE_CR,               .act1 = header_value_end,},
    {.when = '\n',                  .dest = HEADER_LINE_CRLF,             .act1 = header_value_end,},
};

static const struct parser_state_transition ST_HEADER_LINE_CR [] =  {
    {.when = '\n',                  .dest = HEADER_LINE_CRLF,             .act1 = wait_msg,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_HEADER_LINE_CRLF [] =  {
    {.when = '\r',                  .dest = HEADERS_ENDLINE_CR,           .act1 = wait_msg,},
    {.when = '\n',                  .dest = HEADERS_ENDLINE_CRLF,         .act1 = header_section_end,},
    {.when = TOKEN_ALPHA,           .dest = HEADER_NAME,                  .act1 = header_name,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_HEADERS_ENDLINE_CR [] =  {
    {.when = '\n',                  .dest = HEADERS_ENDLINE_CRLF,         .act1 = header_section_end,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_HEADERS_ENDLINE_CRLF [] =  {
    {.when = ANY,                   .dest = BODY,                          .act1 = body,},
};

static const struct parser_state_transition ST_BODY [] =  {
    {.when = ANY,                   .dest = BODY,                          .act1 = body,},
};

static const struct parser_state_transition ST_UNEXPECTED [] =  {
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

///////////////////////////////////////////////////////////////////////////////
// Declaración formal

static const struct parser_state_transition *states [] = {
    ST_METHOD,
    ST_TARGET,
    ST_VERSION,
    ST_REQ_LINE_CR,
    ST_REQ_LINE_CRLF,
    ST_HEADER_NAME,
    ST_HEADER_VALUE,
    ST_HEADER_LINE_CR,
    ST_HEADER_LINE_CRLF,
    ST_HEADERS_ENDLINE_CR,
    ST_HEADERS_ENDLINE_CRLF,
    ST_BODY,
    ST_UNEXPECTED,
};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static const size_t states_n [] = {
    N(ST_METHOD),
    N(ST_TARGET),
    N(ST_VERSION),
    N(ST_REQ_LINE_CR),
    N(ST_REQ_LINE_CRLF),
    N(ST_HEADER_NAME),
    N(ST_HEADER_VALUE),
    N(ST_HEADER_LINE_CR),
    N(ST_HEADER_LINE_CRLF),
    N(ST_HEADERS_ENDLINE_CR),
    N(ST_HEADERS_ENDLINE_CRLF),
    N(ST_BODY),
    N(ST_UNEXPECTED),
};

static struct parser_definition definition = {
        .states_count = N(states),
        .states       = states,
        .states_n     = states_n,
        .start_state  = METHOD,
};

//////////////////////////////////////////////////////////////////////////////

// Functions

#define MIN(x,y) (x < y ? x : y)

#define COPY(dst, src, srcBytes) memcpy(dst, src, MIN(srcBytes, N(dst)));

void http_parser_init(struct parserData * data){
    if(data != NULL){
        data->parser = parser_init(init_char_class(), &definition);
        buffer_init(&(data->parseBuffer), PARSE_BUFF_SIZE, malloc(PARSE_BUFF_SIZE));
    }
}

void http_parser_reset(parserData * data){
    parser_reset(data->parser);
    buffer_reset(&(data->parseBuffer));
}

void http_parser_destroy(parserData * data){
    parser_destroy(data->parser);
    free(data->parseBuffer.data);
    free(data);
}

void assign_method(struct request * httpRequest, parserData * data){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(data->parseBuffer), &size);

    if(strncmp(ptr, "GET", size) == 0)
        httpRequest->method = GET;
    if(strncmp(ptr, "POST", size) == 0)
        httpRequest->method = POST;
    if(strncmp(ptr, "CONNECT", size) == 0)
        httpRequest->method = CONNECT;
}

void assign_target(struct request * req, parserData * data){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(data->parseBuffer), &size);

    COPY(req->url, ptr, size);
}

void assign_version(struct request * req, parserData * data){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(data->parseBuffer), &size);

    COPY(req->version, ptr, size);
}

void assign_header_name(struct request * req, parserData * data){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(data->parseBuffer), &size);

    COPY(req->headers[req->header_count][0], ptr, size);
}

void assign_header_value(struct request * req, parserData * data){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(data->parseBuffer), &size);

    COPY(req->headers[req->header_count][1], ptr, size);
    req->header_count += 1;
}


parse_state http_parser_parse(buffer * readBuffer, struct request * httpRequest, parserData * data) {

    parse_state result = PENDING;

    while(buffer_can_read(readBuffer)){

        const struct parser_event * e = parser_feed(data->parser, buffer_read(readBuffer));

        switch(e->type) {
            case METHOD_NAME:
                log(DEBUG, "METHOD_NAME %c", e->data[0]);
                buffer_write(&(data->parseBuffer), toupper(e->data[0]));
                break;

            case METHOD_NAME_END:
                log(DEBUG, "METHOD_NAME_END %c", e->data[0]);
                assign_method(httpRequest, data);
                buffer_reset(&(data->parseBuffer));
                break;

            case TARGET_VAL:
                log(DEBUG, "TARGET_VAL %c", e->data[0]);
                buffer_write(&(data->parseBuffer), tolower(e->data[0]));
                break;

            case TARGET_VAL_END:
                log(DEBUG, "TARGET_VAL_END %c", e->data[0]);
                assign_target(httpRequest, data);
                buffer_reset(&(data->parseBuffer));
                break;

            case VERSION_VAL:
                log(DEBUG, "VERSION_VAL %c", e->data[0]);
                buffer_write(&(data->parseBuffer), toupper(e->data[0]));
                break;

            case VERSION_VAL_END:
                log(DEBUG, "VERSION_VAL_END %c", e->data[0]);
                assign_version(httpRequest, data);
                buffer_reset(&(data->parseBuffer));
                break;

            case HEADER_NAME_VAL:
                log(DEBUG, "HEADER_NAME %c", e->data[0]);
                buffer_write(&(data->parseBuffer), e->data[0]);
                break;

            case HEADER_NAME_END:
                log(DEBUG, "HEADER_NAME_END %c", e->data[0]);
                assign_header_name(httpRequest, data);
                buffer_reset(&(data->parseBuffer));
                break;

            case HEADER_VALUE_VAL:
                log(DEBUG, "HEADER_VAL %c", e->data[0]);
                buffer_write(&(data->parseBuffer), e->data[0]);
                break;
            
            case HEADER_VALUE_END:
                log(DEBUG, "HEADER_VAL_END %c", e->data[0]);
                assign_header_value(httpRequest, data);
                buffer_reset(&(data->parseBuffer));
                break;

            case HEADER_SECTION_END:
                log(DEBUG, "HEADER_SECTION_END %c", e->data[0]);
                http_parser_reset(data);
                result = SUCCESS;
                break;

            case BODY_VAL:
                log(DEBUG, "BODY_VAL %c", e->data[0]);
                httpRequest->body[httpRequest->body_length++] = e->data[0];
                http_parser_reset(data);
                result = SUCCESS;
                break;

            case WAIT_MSG:
                log(DEBUG, "WAIT_MSG %c",e->data[0]);
                break;

            case UNEXPECTED_VALUE:
                log(DEBUG, "UNEXPECTED_VALUE %d", e->data[0]);
                http_parser_reset(data);
                return FAILED;

            default:
                log(ERROR, "Unexpected event type %d", e->type);
                return FAILED;
        }
     
    }

    return result;

}
