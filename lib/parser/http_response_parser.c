#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <abnf_chars.h>
#include <parser.h>
#include <logger.h>
#include <http_response_parser.h>

#define PARSE_BUFF_SIZE 1024

#pragma GCC diagnostic ignored "-Wunused-variable"

///////////////////////////////////////////////////////////////////////////////
// STATES AND EVENTS

static char state_names[6][24] = {
    "VERSION", "STATUS", "REASON", "RES_LINE_CR", "RES_LINE_CRLF", "UNEXPECTED"
};

enum states {
    VERSION,
    STATUS,
    REASON,

    RES_LINE_CR,
    RES_LINE_CRLF,

    UNEXPECTED
};

static char event_names[8][24] = {
    "VERSION_VAL", "VERSION_VAL_END", "STATUS_VAL", "STATUS_VAL_END", "REASON_VAL", "REASON_VAL_END",
    "WAIT_MSG", "UNEXPECTED_VALUE"
};

enum event_type{
    VERSION_VAL,
    VERSION_VAL_END,

    STATUS_VAL,
    STATUS_VAL_END,

    REASON_VAL,
    REASON_VAL_END,
  
    WAIT_MSG,
    UNEXPECTED_VALUE
};


///////////////////////////////////////////////////////////////////////////////
// ACTIONS

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

static void status(struct parser_event *ret, const uint8_t c) {
    ret->type    = STATUS_VAL;
    ret->n       = 1;
    ret->data[0] = c;
}

static void status_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = STATUS_VAL_END;
    ret->n       = 1;
    ret->data[0] = c;
}

static void reason(struct parser_event *ret, const uint8_t c) {
    ret->type    = REASON_VAL;
    ret->n       = 1;
    ret->data[0] = c;
}

static void reason_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = REASON_VAL_END;
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

static const struct parser_state_transition ST_VERSION [] =  {
    {.when = TOKEN_SP,              .dest = STATUS,                       .act1 = version_end,},
    {.when = TOKEN_VCHAR,           .dest = VERSION,                      .act1 = version,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_STATUS [] =  {
    {.when = TOKEN_DIGIT,           .dest = STATUS,                       .act1 = status,},
    {.when = TOKEN_SP,                   .dest = REASON,                  .act1 = status_end,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_REASON [] =  {
    {.when = TOKEN_HTAB,            .dest = REASON,                       .act1 = reason,},
    {.when = TOKEN_VCHAR,           .dest = REASON,                       .act1 = reason,},
    {.when = TOKEN_SP,              .dest = REASON,                       .act1 = reason,},
    {.when = TOKEN_CR,              .dest = RES_LINE_CR,                  .act1 = reason_end,},
    {.when = TOKEN_LF,              .dest = RES_LINE_CRLF,                .act1 = reason_end,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_RES_LINE_CR[] =  {
    {.when = TOKEN_LF,              .dest = RES_LINE_CRLF,                .act1 = wait_msg,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_RES_LINE_CRLF[] =  {
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_UNEXPECTED [] =  {
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

///////////////////////////////////////////////////////////////////////////////
// FORMAL DECLARATION

static const struct parser_state_transition *states [] = {
    ST_VERSION,
    ST_STATUS,
    ST_REASON,
    ST_RES_LINE_CR,
    ST_RES_LINE_CRLF,
    ST_UNEXPECTED
};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static const size_t states_n [] = {
    N(ST_VERSION),
    N(ST_STATUS),
    N(ST_REASON),
    N(ST_RES_LINE_CR),
    N(ST_RES_LINE_CRLF),
    N(ST_UNEXPECTED),
};

static struct parser_definition definition = {
    .states_count = N(states),
    .states       = states,
    .states_n     = states_n,
    .start_state  = VERSION,
};


//////////////////////////////////////////////////////////////////////////////
// AUXILIAR FUNCTIONS

#define MIN(x,y) (x < y ? x : y)

#define COPY(dst, src, srcBytes) memcpy(dst, src, MIN(srcBytes, N(dst)));


static void assign_version(http_response * res, http_response_parser * parser){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(parser->parse_buffer), &size);

    COPY(res->version, ptr, size);
}


static void assign_status(http_response * res, http_response_parser * parser){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(parser->parse_buffer), &size);

    res->status = atoi(ptr);
}


static void assign_reason(http_response * res, http_response_parser * parser){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(parser->parse_buffer), &size);

    COPY(res->reason, ptr, size);
}


//////////////////////////////////////////////////////////////////////////////
// PARSER FUNCTIONS

void http_response_parser_init(http_response_parser * parser) {
    if(parser != NULL){
        parser->parser = parser_init(init_char_class(), &definition);
        buffer_init(&(parser->parse_buffer), PARSE_BUFF_SIZE, malloc(PARSE_BUFF_SIZE));

        http_message_parser_init(&(parser->message_parser));
    }
}


void http_response_parser_reset(http_response_parser * parser){
    parser_reset(parser->parser);
    buffer_reset(&(parser->parse_buffer));
    http_message_parser_reset(&(parser->message_parser));
}


void http_response_parser_destroy(http_response_parser * parser){
    parser_destroy(parser->parser);
    http_message_parser_destroy(&(parser->message_parser));
    free(parser->parse_buffer.data);
    free(parser);
}


parse_state http_response_parser_parse(http_response_parser * parser, buffer * read_buffer, http_response * response) {

    while(buffer_can_read(read_buffer)){

        if (parser->parser->state != RES_LINE_CRLF) {
            const struct parser_event * e = parser_feed(parser->parser, buffer_read(read_buffer));

            log(DEBUG, "STATE %s", state_names[parser->parser->state]);
            log(DEBUG, "%s %c", event_names[e->type], e->data[0]);

            switch(e->type) {
                case VERSION_VAL:
                    buffer_write(&(parser->parse_buffer), toupper(e->data[0]));
                    break;

                case VERSION_VAL_END:
                    assign_version(parser->response, parser);
                    buffer_reset(&(parser->parse_buffer));
                    break;

                case STATUS_VAL:
                    buffer_write(&(parser->parse_buffer), e->data[0]);
                    break;

                case STATUS_VAL_END:
                    assign_status(parser->response, parser);
                    buffer_reset(&(parser->parse_buffer));
                    break;

                case REASON_VAL:
                    buffer_write(&(parser->parse_buffer),e->data[0]);
                    break;

                case REASON_VAL_END:
                    assign_reason(parser->response, parser);
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

            parse_state result = http_message_parser_parse(&(parser->message_parser), read_buffer, &(response->message));

            if (result == FAILED)
                parser->error_code = parser->message_parser.error_code;

            if (result == SUCCESS || result == FAILED)
                return result;

        }
     
    }

    return PENDING;

}
