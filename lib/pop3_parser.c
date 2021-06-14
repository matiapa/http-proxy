#include "pop3_parser.h"

#include<stdio.h>
#include <stdlib.h>  // malloc
#include <string.h>  // memset
#include <ctype.h> //toUpper

#include <pop3_parser.h>
#include <abnf_chars.h>
#include <parser.h>
#include <logger.h>

#define PARSE_BUFF_SIZE 1024

enum states {
    COMMAND,
    COMMAND_VAL,
    COMMAND_CR,
    UNEXPECTED,
};

enum event_type{
    COMMAND_NAME,
    COMMAND_NAME_END,

    COMMAND_VALUE,
    COMMAND_VALUE_END,

    WAIT_MSG,
    UNEXPECTED_VALUE
};


///////////////////////////////////////////////////////////////////////////////
// Acciones

static void command(struct parser_event *ret, const uint8_t c) {
    ret->type    = COMMAND_NAME;
    ret->n       = 1;
    ret->data[0] = c;
}

static void command_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = COMMAND_NAME_END;
    ret->n       = 1;
    ret->data[0] = c;
}

static void value(struct parser_event *ret, const uint8_t c) {
    ret->type    = COMMAND_VALUE;
    ret->n       = 1;
    ret->data[0] = c;
}

static void val_end(struct parser_event *ret, const uint8_t c) {
    ret->type    = COMMAND_VALUE_END;
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

static const struct parser_state_transition ST_COMMAND [] =  {
    {.when = TOKEN_ALPHA,           .dest = COMMAND,                       .act1 = command,},
    {.when = TOKEN_DIGIT,           .dest = COMMAND,                       .act1 = command,},
    {.when = ' ',                  .dest = COMMAND_VAL,                    .act1 = command_end,},
    {.when = ANY,                   .dest = UNEXPECTED,                    .act1 = error,},
};

static const struct parser_state_transition ST_COMMAND_VAL [] =  {
    {.when = TOKEN_ALPHA,           .dest = COMMAND_VAL,                   .act1 = value,},
    {.when = '\r',                  .dest = COMMAND_CR,                    .act1 = val_end,},
    {.when = '\n',                  .dest = COMMAND,                       .act1 = val_end,},
    {.when = ANY,                   .dest = UNEXPECTED,                    .act1 = error,},
};

static const struct parser_state_transition ST_COMMAND_CR[] =  {
    {.when = '\n',                  .dest = COMMAND,                       .act1 = wait_msg,},
    {.when = ANY,                   .dest = UNEXPECTED,                    .act1 = error,},
};

static const struct parser_state_transition ST_UNEXPECTED [] =  {
    {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

///////////////////////////////////////////////////////////////////////////////
// Declaración formal

static const struct parser_state_transition *states [] = {
        ST_COMMAND,
        ST_COMMAND_VAL,
        ST_UNEXPECTED,
        ST_COMMAND_CR,
};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static const size_t states_n [] = {
        N(ST_COMMAND),
        N(ST_COMMAND_VAL),
        N(ST_UNEXPECTED),
        N(ST_COMMAND_CR)
};

static struct parser_definition definition = {
        .states_count = N(states),
        .states       = states,
        .states_n     = states_n,
        .start_state  = COMMAND,
};

//////////////////////////////////////////////////////////////////////////////

// Functions

#define MIN(x,y) ((x) < (y) ? (x) : (y))

#define COPY(dst, src, srcBytes) memcpy(dst, src, MIN(srcBytes, N(dst)));

void pop3_parser_init(pop3_parser_data * data){
    if(data != NULL){
        data->parser = parser_init(init_char_class(), &definition);
        buffer_init(&(data->popBuffer), PARSE_BUFF_SIZE, malloc(PARSE_BUFF_SIZE));
    }
}

void pop3_parser_reset(pop3_parser_data * data){
    parser_reset(data->parser);
    buffer_reset(&(data->popBuffer));
    memset(data->user,0,sizeof((*data).user));
    memset(data->pass,0,sizeof((*data).pass));
    
}

void pop3_parser_destroy(pop3_parser_data * data){
    parser_destroy(data->parser);
    free(data->popBuffer.data);
    free(data);
}

void assign_cmd(pop3_parser_data * data){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(data->popBuffer), &size);

    data->last_cmd = POP3_OTHER;

    if(strncmp(ptr, "USER", size) == 0)
        data->last_cmd = POP3_USER;

    if(strncmp(ptr, "PASS", size) == 0)
        data->last_cmd = POP3_PASSWORD;
}

void assign_value(pop3_parser_data * data){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(data->popBuffer), &size);

    if(data->last_cmd == POP3_USER)
        COPY(data->user, ptr, size);

    if(data->last_cmd == POP3_PASSWORD)
        COPY(data->pass, ptr, size);
}


pop3_state pop3_parse(buffer * readBuffer, pop3_parser_data * data) {

    pop3_state result = POP3_PENDING;

    while(buffer_can_read(readBuffer)){

        const struct parser_event * e = parser_feed(data->parser, buffer_read(readBuffer));

        switch(e->type) {
            case COMMAND_NAME:
                buffer_write(&(data->popBuffer), toupper(e->data[0]));
                break;

            case COMMAND_NAME_END:
                assign_cmd(data);
                buffer_reset(&(data->popBuffer));
                break;

            case COMMAND_VALUE:
                buffer_write(&(data->popBuffer), e->data[0]);
                break;

            case COMMAND_VALUE_END:
                assign_value(data);
                buffer_reset(&(data->popBuffer));
                result = POP3_SUCCESS;
                break;

            case WAIT_MSG:
                break;

            case UNEXPECTED_VALUE:
                pop3_parser_reset(data);
                return POP3_FAILED;

            default:
                log(ERROR, "Unexpected event type %d", e->type);
                return POP3_FAILED;
        }

    }

    return result;

}
