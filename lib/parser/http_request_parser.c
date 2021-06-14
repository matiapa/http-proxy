#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <abnf_chars.h>
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
    {.when = TOKEN_SP,                   .dest = TARGET,                       .act1 = method_end,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_TARGET [] =  {
    {.when = TOKEN_SP,                   .dest = VERSION,                      .act1 = target_end,},
    {.when = TOKEN_VCHAR,           .dest = TARGET,                       .act1 = target,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_VERSION [] =  {
    {.when = TOKEN_VCHAR,           .dest = VERSION,                      .act1 = version,},
    {.when = TOKEN_CR,              .dest = REQ_LINE_CR,                  .act1 = version_end,},
    {.when = TOKEN_LF,              .dest = REQ_LINE_CRLF,                .act1 = version_end,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_REQ_LINE_CR[] =  {
    {.when = TOKEN_LF,              .dest = REQ_LINE_CRLF,                .act1 = wait_msg,},
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


void assign_method(http_request_parser * parser){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(parser->parse_buffer), &size);

    for (size_t i = 0; i < N(methods_strings); i++) {
        if(strncmp(ptr, methods_strings[i], size) == 0) {
            parser->request.method = i + 1;
            break;
        }
    }
}


parse_state assign_target(http_request_parser * parser) {
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(parser->parse_buffer), &size);
    
    if (N(parser->request.url) <= size) {
        parser->error_code = URI_TOO_LONG;
        return ERROR;
    }

    COPY(parser->request.url, ptr, size);

    return SUCCESS;
}

static void assign_version(http_request_parser * parser){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(parser->parse_buffer), &size);

    COPY(parser->request.version, ptr, size);
}


//////////////////////////////////////////////////////////////////////////////
// PARSER FUNCTIONS

void http_request_parser_init(http_request_parser * parser) {
    if(parser != NULL){
        memset(&(parser->request), 0, sizeof(http_request));
        parser->error_code = -1;

        parser->parser = parser_init(init_char_class(), &definition);
        http_message_parser_init(&(parser->message_parser));
        buffer_init(&(parser->parse_buffer), PARSE_BUFF_SIZE, malloc(PARSE_BUFF_SIZE));
    }
}


void http_request_parser_reset(http_request_parser * parser){
    memset(&(parser->request), 0, sizeof(http_request));
    parser->error_code = -1;

    parser_reset(parser->parser);
    http_message_parser_reset(&(parser->message_parser));
    buffer_reset(&(parser->parse_buffer));
}


void http_request_parser_destroy(http_request_parser * parser){
    parser_destroy(parser->parser);
    http_message_parser_destroy(&(parser->message_parser));

    free(parser->parse_buffer.data);
    free(parser);
}


parse_state http_request_parser_parse(http_request_parser * parser, buffer * read_buffer) {

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
                    assign_method(parser);
                    buffer_reset(&(parser->parse_buffer));
                    break;

                case TARGET_VAL:
                    buffer_write(&(parser->parse_buffer), tolower(e->data[0]));
                    break;

                case TARGET_VAL_END:
                    s = assign_target(parser);
                    if (s == FAILED)
                        return FAILED;
                    buffer_reset(&(parser->parse_buffer));
                    break;

                case VERSION_VAL:
                    buffer_write(&(parser->parse_buffer), toupper(e->data[0]));
                    break;

                case VERSION_VAL_END:
                    assign_version(parser);
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

            bool ignore_content_length = parser->request.method == HEAD;

            parse_state result = http_message_parser_parse(
                &(parser->message_parser), read_buffer, &(parser->request.message),
                ignore_content_length
            );

            if (result == FAILED)
                parser->error_code = parser->message_parser.error_code;

            return result;

        }
     
    }

    return PENDING;

}
