#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <abnf_chars.h>
#include <parser.h>
#include <logger.h>
#include <http_message_parser.h>

#define PARSE_BUFF_SIZE 1024

#pragma GCC diagnostic ignored "-Wunused-variable"

///////////////////////////////////////////////////////////////////////////////
// STATES AND EVENTS

static char state_names[9][24] = {
    "HEADERS_BEGIN", "HEADER_NAME", "HEADER_VALUE", "HEADER_LINE_CR", "HEADER_LINE_CRLF",
    "HEADERS_ENDLINE_CR", "HEADERS_ENDLINE_CRLF", "BODY", "UNEXPECTED"
};

enum states {
    HEADERS_BEGIN,

    HEADER_NAME,
    HEADER_VALUE,

    HEADER_LINE_CR,
    HEADER_LINE_CRLF,

    HEADERS_ENDLINE_CR,
    HEADERS_ENDLINE_CRLF,

    BODY,

    UNEXPECTED
};


static char event_names[8][24] = {
    "HEADER_NAME_VAL", "HEADER_NAME_END", "HEADER_VALUE_VAL", "HEADER_VALUE_END", "HEADERS_SECTION_END",
    "BODY_VAL", "WAIT_MSG", "UNEXPECTED_VALUE"
};

enum event_type{
    HEADER_NAME_VAL,
    HEADER_NAME_END,

    HEADER_VALUE_VAL,
    HEADER_VALUE_END,

    HEADER_SECTION_END,

    BODY_VAL,

    WAIT_MSG,
    UNEXPECTED_VALUE
};


///////////////////////////////////////////////////////////////////////////////
// ACTIONS

static void header_name(struct parser_event *ret, const uint8_t c) {
    ret->type    = HEADER_NAME_VAL;
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
// TRANSITIONS

static const struct parser_state_transition ST_HEADERS_BEGIN [] =  {
    {.when = TOKEN_CR,              .dest = HEADERS_ENDLINE_CR,           .act1 = wait_msg,},
    {.when = TOKEN_LF,              .dest = HEADERS_ENDLINE_CRLF,         .act1 = header_section_end,},
    {.when = TOKEN_ALPHA,           .dest = HEADER_NAME,                  .act1 = header_name,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_HEADER_NAME [] =  {
    {.when = ':',                   .dest = HEADER_VALUE,                 .act1 = header_name_end,},
    {.when = TOKEN_SP,              .dest = HEADER_NAME,                  .act1 = wait_msg,},
    {.when = TOKEN_VCHAR,           .dest = HEADER_NAME,                  .act1 = header_name,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_HEADER_VALUE [] =  {
    {.when = TOKEN_VCHAR,           .dest = HEADER_VALUE,                 .act1 = header_value,},
    {.when = TOKEN_SP,              .dest = HEADER_VALUE,                 .act1 = header_value,},
    {.when = TOKEN_CR,              .dest = HEADER_LINE_CR,               .act1 = header_value_end,},
    {.when = TOKEN_LF,              .dest = HEADER_LINE_CRLF,             .act1 = header_value_end,},
};

static const struct parser_state_transition ST_HEADER_LINE_CR [] =  {
    {.when = TOKEN_LF,              .dest = HEADER_LINE_CRLF,             .act1 = wait_msg,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_HEADER_LINE_CRLF [] =  {
    {.when = TOKEN_CR,              .dest = HEADERS_ENDLINE_CR,           .act1 = wait_msg,},
    {.when = TOKEN_LF,              .dest = HEADERS_ENDLINE_CRLF,         .act1 = header_section_end,},
    {.when = TOKEN_VCHAR,           .dest = HEADER_NAME,                  .act1 = header_name,},
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_HEADERS_ENDLINE_CR [] =  {
    {.when = TOKEN_LF,              .dest = HEADERS_ENDLINE_CRLF,         .act1 = header_section_end,},
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
// FORMAL DECLARATION

static const struct parser_state_transition *states [] = {
    ST_HEADERS_BEGIN,
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
    N(ST_HEADERS_BEGIN),
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
    .start_state  = HEADERS_BEGIN,
};


//////////////////////////////////////////////////////////////////////////////
// AUXILIAR FUNCTIONS

#define MIN(x,y) ((x) < (y) ? (x) : (y))

#define COPY(dst, src, srcBytes) memcpy(dst, src, MIN(srcBytes, N(dst)));

static void assign_header_name(http_message * message, http_message_parser * parser){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(parser->parse_buffer), &size);

    if (message->header_count >= N(message->headers))
        return;

    COPY(message->headers[message->header_count][0], ptr, size);
}

static int assign_header_value(http_message * message, http_message_parser * parser, bool ignore_length){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(parser->parse_buffer), &size);

    if (message->header_count >= N(message->headers))
        return 0;

    COPY(message->headers[message->header_count][1], ptr, size);

    if (!ignore_length
        && strncmp(message->headers[message->header_count][0], "Content-Length", HEADER_LENGTH) == 0
    ) {
        message->body_length = atoi(message->headers[message->header_count][1]);
        log(DEBUG, "Found Content-Length: %lu", message->body_length);
    }

    if (strncmp(message->headers[message->header_count][0], "Expect", HEADER_LENGTH) == 0) {
        message->hasExpect = true;
        log(DEBUG, "Found Expect header");
    }

    if (strncmp(message->headers[message->header_count][0], "Transfer-Encoding", HEADER_LENGTH) == 0) {
        char * value = message->headers[message->header_count][1];
        while(*value == ' ') value++;
        log(DEBUG, "Found Transfer-Encoding: %s", value);
        
        if (strcmp(value, "chunked") == 0)
            return NOT_IMPLEMENTED;
    }

    message->header_count += 1;

    return 0;
}


//////////////////////////////////////////////////////////////////////////////
// PARSER FUNCTIONS

void http_message_parser_init(http_message_parser * parser){
    if(parser != NULL){
        parser->parser = parser_init(init_char_class(), &definition);
        buffer_init(&(parser->parse_buffer), PARSE_BUFF_SIZE, malloc(PARSE_BUFF_SIZE));
        parser->current_body_length = 0;
    }
}


void http_message_parser_reset(http_message_parser * parser){
    parser_reset(parser->parser);
    buffer_reset(&(parser->parse_buffer));
    parser->current_body_length = 0;
}


void http_message_parser_destroy(http_message_parser * parser){
    parser_destroy(parser->parser);
    free(parser->parse_buffer.data);
}


parse_state http_message_parser_parse(
    http_message_parser * parser, buffer * read_buffer, http_message * message, bool ignore_content_length
) {

    while(buffer_can_read(read_buffer)){
        size_t nbytes;
        char * pointer = (char *)buffer_read_ptr(read_buffer, &nbytes);

        const struct parser_event * e = parser_feed(parser->parser, buffer_read(read_buffer));

        // log(DEBUG, "STATE %s", state_names[parser->parser->state]);
        // log(DEBUG, "%s %c", event_names[e->type], e->data[0]);

        int res;

        switch(e->type) {
            case HEADER_NAME_VAL:
                buffer_write(&(parser->parse_buffer), e->data[0]);
                break;

            case HEADER_NAME_END:
                assign_header_name(message, parser);
                buffer_reset(&(parser->parse_buffer));
                break;

            case HEADER_VALUE_VAL:
                buffer_write(&(parser->parse_buffer), e->data[0]);
                break;
            
            case HEADER_VALUE_END:
                res = assign_header_value(message, parser, ignore_content_length);
                buffer_reset(&(parser->parse_buffer));
                if (res > 0) {
                    parser->error_code = res;
                    return FAILED;
                }
                break;

            case HEADER_SECTION_END:
                message->body = pointer + 1;
                http_message_parser_reset(parser);
                return SUCCESS;
                break;

            case BODY_VAL:
                break;

            case WAIT_MSG:
                break;

            case UNEXPECTED_VALUE:
                parser->error_code = BAD_REQUEST;
                return FAILED;

            default:
                log(ERROR, "Unexpected event type %u", e->type);
                parser->error_code = INTERNAL_SERVER_ERROR;
                return FAILED;
        }
     
    }

    return PENDING;

}
