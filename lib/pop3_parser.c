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

typedef enum cmd_type{
    USER,
    PASSWORD,
    OTHER,
}cmd_type;

enum states {
    COMMAND,
    COMMAND_VAL,

    UNEXPECTED,

    COMMAND_CR,
    COMMAND_CRLF,

};

enum event_type{
    COMMAND_NAME,
    COMMAND_NAME_END,

    COMMAND_VALUE,
    COMMAND_VALUE_END,

    WAIT_MSG,

    UNEXPECTED_VALUE,

    END_TYPE,
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
static void end(struct parser_event *ret, const uint8_t c) {
    ret->type    = END_TYPE;
    ret->n       = 1;
    ret->data[0] = c;
}

///////////////////////////////////////////////////////////////////////////////
// Transiciones

static const struct parser_state_transition ST_COMMAND [] =  {
        {.when = TOKEN_ALPHA,           .dest = COMMAND,                       .act1 = command,},
        {.when = TOKEN_DIGIT,           .dest = COMMAND,                       .act1 = command,},
        {.when = ' ',                  .dest = COMMAND_VAL,                  .act1 = command_end,},
        //{.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_COMMAND_VAL [] =  {
        {.when = TOKEN_ALPHA,           .dest = COMMAND_VAL,                       .act1 = value,},
        {.when = '\r',                  .dest = COMMAND_CR,                  .act1 = val_end,},
        {.when = '\n',                  .dest = COMMAND_CRLF,                .act1 = val_end,},
       {.when = ANY,                   .dest = COMMAND_VAL,                   .act1 = value,},
};

static const struct parser_state_transition ST_COMMAND_CR[] =  {
        {.when = '\n',                  .dest = COMMAND_CRLF,                .act1 = end,},
        {.when = ANY,                   .dest = UNEXPECTED,                   .act1 = error,},
};

static const struct parser_state_transition ST_COMMAND_CRLF[] =  {
        {.when = ANY,                   .dest = COMMAND_CRLF,                   .act1 = end,},
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
        ST_COMMAND_CRLF,

};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static const size_t states_n [] = {
        N(ST_COMMAND),
        N(ST_COMMAND_VAL),
        N(ST_UNEXPECTED),
        N(ST_COMMAND_CR),
        N(ST_COMMAND_CRLF),
};

static struct parser_definition definition = {
        .states_count = N(states),
        .states       = states,
        .states_n     = states_n,
        .start_state  = COMMAND,
};

//////////////////////////////////////////////////////////////////////////////

// Functions

#define MIN(x,y) (x < y ? x : y)

#define COPY(dst, src, srcBytes) memcpy(dst, src, MIN(srcBytes, N(dst)));

void pop3_parser_init(pop3_parser_data * data){
    if(data != NULL){
        data->parser = parser_init(init_char_class(), &definition);
        buffer_init(&(data->popBuffer), PARSE_BUFF_SIZE, malloc(PARSE_BUFF_SIZE));
        data->pass = 0;
    }
}

void pop3_parser_reset(pop3_parser_data * data){
    parser_reset(data->parser);
    buffer_reset(&(data->popBuffer));
}

void pop3_parser_destroy(pop3_parser_data * data){
    parser_destroy(data->parser);
    free(data->popBuffer.data);
    //free(data);
}

cmd_type check_cmd(pop3_parser_data * data){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(data->popBuffer), &size);

    if(strncmp(ptr, "USER", size) == 0){
        return USER;
    }
    if(strncmp(ptr, "PASS", size) == 0){
        return PASSWORD;
    }
    return OTHER;
}

void assign_value( pop3_parser_data * data, cmd_type cmd){
    size_t size;
    char * ptr = (char *) buffer_read_ptr(&(data->popBuffer), &size);
    if (cmd == USER){
        COPY(data->user_pass[0], ptr, size);
        data->user = 1;
    }
    else{
        COPY(data->user_pass[1], ptr, size);
        data->pass = 1;
    }
}



pop3_state pop3_parse(buffer * readBuffer, pop3_parser_data * data, char * pop3_credentials) {

    pop3_state result = POP3_PENDING;
    cmd_type cmd;
    while(buffer_can_read(readBuffer)){

        const struct parser_event * e = parser_feed(data->parser, buffer_read(readBuffer));

        switch(e->type) {
            case COMMAND_NAME:
                //log(DEBUG, "METHOD_NAME %c", e->data[0]);
                buffer_write(&(data->popBuffer), toupper(e->data[0]));
                break;

            case COMMAND_NAME_END:

                cmd = check_cmd(data);

                //log(DEBUG, "METHOD_NAME_END %c", e->data[0]);
                if(cmd == OTHER){
                    pop3_parser_reset(data);
                    return NO_USER_PASS;
                }
                buffer_reset(&(data->popBuffer));
                break;

            case COMMAND_VALUE:
                log(DEBUG, "TARGET_VAL %c", e->data[0]);
                buffer_write(&(data->popBuffer), e->data[0]);
                break;

            case COMMAND_VALUE_END:
                log(DEBUG, "TARGET_VAL_END %c", e->data[0]);
                assign_value(data, cmd);
                if(data->pass == 1 && data->user == 0){
                    data->pass = 0;
                    pop3_parser_reset(data);
                    return FAILED_PASS_NO_USER;
                }
                buffer_reset(&(data->popBuffer));
                break;

            case WAIT_MSG:
                //log(DEBUG, "VERSION_VAL %c", e->data[0]);
                break;

            case UNEXPECTED_VALUE:
                //log(DEBUG, "UNEXPECTED_VALUE %d", e->data[0]);
                data->pass = 0;
                data->user = 0;
                pop3_parser_reset(data);
                return POP3_FAILED;

            case END_TYPE:
                pop3_parser_reset(data);
                if (data->pass && data->user){
                    strcpy(pop3_credentials, "USER: ");
                    strcat(pop3_credentials, data->user_pass[0]);
                    strcat(pop3_credentials, " PASS: ");
                    strcat(pop3_credentials, data->user_pass[1]);
                    data->user = 0;
                    data->pass = 0;
                    return USER_PASS_SUCCESS;
                }
                return USER_SUCCESS;
                break;

            default:
                //log(ERROR, "Unexpected event type %d", e->type);
                return POP3_FAILED;
        }

    }

    return result;

}
