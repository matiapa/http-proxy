#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <http_chars.h>
#include <parser.h>
#include <logger.h>
#include <http.h>
#include <http_request_parser.h>

#define PARSE_BUFF_SIZE 1024

#pragma GCC diagnostic ignored "-Wunused-variable"

///////////////////////////////////////////////////////////////////////////////
// STATES AND EVENTS

static char state_names[6][24] = {
    "METHOD", "TARGET", "VERSION", "REQ_LINE_CR", "REQ_LINE_CRLF", "UNEXPECTED"
};

enum states {
    METHOD,
    TARGET,
    VERSION,

    REQ_LINE_CR,
    REQ_LINE_CRLF,

    UNEXPECTED
};

static char event_names[8][24] = {
    "METHOD_NAME", "METHOD_NAME_END", "TARGET_VAL", "TARGET_VAL_END", "VERSION_VAL", "VERSION_VAL_END",
    "WAIT_MSG", "UNEXPECTED_VALUE"
};

enum event_type{
    METHOD_NAME,
    METHOD_NAME_END,

    TARGET_VAL,
    TARGET_VAL_END,

    VERSION_VAL,
    VERSION_VAL_END,
  
    WAIT_MSG,
    UNEXPECTED_VALUE
};


///////////////////////////////////////////////////////////////////////////////
// ACTIONS

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
// TRANSITIONS

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
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_UNEXPECTED [] =  {
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

///////////////////////////////////////////////////////////////////////////////
// FORMAL DECLARATION

static const struct parser_state_transition *states [] = {
    ST_METHOD,
    ST_TARGET,
    ST_VERSION,
    ST_REQ_LINE_CR,
    ST_REQ_LINE_CRLF,
    ST_UNEXPECTED
};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static const size_t states_n [] = {
    N(ST_METHOD),
    N(ST_TARGET),
    N(ST_VERSION),
    N(ST_REQ_LINE_CR),
    N(ST_REQ_LINE_CRLF),
    N(ST_UNEXPECTED),
};

static struct parser_definition definition = {
    .states_count = N(states),
    .states       = states,
    .states_n     = states_n,
    .start_state  = METHOD,
};


//////////////////////////////////////////////////////////////////////////////
// AUXILIAR FUNCTIONS

#define MIN(x,y) (x < y ? x : y)

#define COPY(dst, src, srcBytes) memcpy(dst, src, MIN(srcBytes, N(dst)));


void assign_method(http_request * httpRequest, http_request_parser * parser){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(parser->parse_buffer), &size);

    if(strncmp(ptr, "GET", size) == 0)
        httpRequest->method = GET;
    else if(strncmp(ptr, "POST", size) == 0)
        httpRequest->method = POST;
    else if(strncmp(ptr, "CONNECT", size) == 0)
        httpRequest->method = CONNECT;
    else if(strncmp(ptr, "DELETE", size) == 0)
        httpRequest->method = DELETE;
    else if(strncmp(ptr, "PUT", size) == 0)
        httpRequest->method = PUT;
    else if(strncmp(ptr, "HEAD", size) == 0)
        httpRequest->method = HEAD;
    else
        httpRequest->method = OTHER;

    parser->message_parser.method = httpRequest->method;
}


parse_state assign_target(http_request * req, http_request_parser * parser) {
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(parser->parse_buffer), &size);
    
    if (N(req->url) <= size) {
        parser->error_code = URI_TOO_LONG;
        return ERROR;
    }

    COPY(req->url, ptr, size);

    return SUCCESS;
}

static void assign_version(http_request * req, http_request_parser * parser){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(parser->parse_buffer), &size);

    COPY(req->version, ptr, size);
}


//////////////////////////////////////////////////////////////////////////////
// PARSER FUNCTIONS

void http_request_parser_init(http_request_parser * parser) {
    if(parser != NULL){
        parser->parser = parser_init(init_char_class(), &definition);
        buffer_init(&(parser->parse_buffer), PARSE_BUFF_SIZE, malloc(PARSE_BUFF_SIZE));

        http_message_parser_init(&(parser->message_parser));
    }
}


void http_request_parser_reset(http_request_parser * parser){
    parser_reset(parser->parser);
    buffer_reset(&(parser->parse_buffer));
    http_message_parser_reset(&(parser->message_parser));
}


void http_request_parser_destroy(http_request_parser * parser){
    parser_destroy(parser->parser);
    http_message_parser_destroy(&(parser->message_parser));
    free(parser->parse_buffer.data);
    free(parser);
}


parse_state http_request_parser_parse(http_request_parser * parser, buffer * read_buffer, http_request * request) {

    while(buffer_can_read(read_buffer)){

        if (parser->parser->state != REQ_LINE_CRLF) {
            const struct parser_event * e = parser_feed(parser->parser, buffer_read(read_buffer));

            // log(DEBUG, "STATE %s", state_names[parser->parser->state]);
            // log(DEBUG, "%s %c", event_names[e->type], e->data[0]);

            parse_state s;

            switch(e->type) {
                case METHOD_NAME:
                    buffer_write(&(parser->parse_buffer), toupper(e->data[0]));
                    break;

                case METHOD_NAME_END:
                    assign_method(parser->request, parser);
                    buffer_reset(&(parser->parse_buffer));
                    break;

                case TARGET_VAL:
                    buffer_write(&(parser->parse_buffer), tolower(e->data[0]));
                    break;

                case TARGET_VAL_END:
                    s = assign_target(parser->request, parser);
                    if (s == FAILED)
                        return FAILED;
                    buffer_reset(&(parser->parse_buffer));
                    break;

                case VERSION_VAL:
                    buffer_write(&(parser->parse_buffer), toupper(e->data[0]));
                    break;

                case VERSION_VAL_END:
                    assign_version(parser->request, parser);
                    buffer_reset(&(parser->parse_buffer));
                    break;

                case WAIT_MSG:
                    break;

                case UNEXPECTED_VALUE:
                    parser->error_code = BAD_REQUEST;
                    return FAILED;

                default:
                    log(ERROR, "Unexpected event type %d", e->type);
                    parser->error_code = INTERNAL_SERVER_ERROR;
                    return FAILED;
            }

        } else {

            parse_state result = http_message_parser_parse(&(parser->message_parser), read_buffer, &(request->message));

            if (result == FAILED)
                parser->error_code = parser->message_parser.error_code;

            if (result == SUCCESS || result == FAILED)
                return result;

        }
     
    }

    return PENDING;

}
