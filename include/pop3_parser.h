#ifndef POP3_PARSER_H
#define POP3_PARSER_H

#include <parser.h>
#include <buffer.h>

#define MAX_USER_PASSWORD_LENGTH 25
#define MAX_CREDENTIALS_LENGTH 100

typedef enum cmd_type {
    POP3_USER,
    POP3_PASSWORD,
    POP3_OTHER,
} cmd_type;

typedef enum pop3_state {
    POP3_PENDING,
    POP3_SUCCESS,
    POP3_FAILED
} pop3_state;

typedef struct pop3_parser_data {
    struct parser * parser;
    buffer  popBuffer;
    
    cmd_type last_cmd;
    char * user;
    char * pass;
    int user_len;
    int pass_len;
} pop3_parser_data;

void pop3_parser_init(pop3_parser_data * data);

pop3_state pop3_parse(buffer * readBuffer, pop3_parser_data * data);

void pop3_parser_reset(pop3_parser_data * data);

void pop3_parser_destroy(pop3_parser_data * data);

#endif
