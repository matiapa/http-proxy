#ifndef POP3_PARSER_H
#define POP3_PARSER_H

#include <parser.h>
#include <buffer.h>

#define MAX_USER_PASSWORD_LENGTH 25
#define MAX_CREDENTIALS_LENGTH 100


typedef enum pop3_state{
    POP3_FAILED,
    FAILED_PASS_NO_USER,
    NO_USER_PASS,
    POP3_PENDING,
    USER_SUCCESS,
    USER_PASS_SUCCESS,
}pop3_state;

typedef struct pop3_parser_data{
    struct parser * parser;
    buffer  popBuffer;
    char user_pass[2][MAX_USER_PASSWORD_LENGTH];
    int pass;
    int user;
    char credentials[MAX_CREDENTIALS_LENGTH];
}pop3_parser_data;

void pop3_parser_init(pop3_parser_data * data);

pop3_state pop3_parse(buffer * readBuffer, pop3_parser_data * data, char * pop3_credentials);

void pop3_parser_destroy(pop3_parser_data * data);

#endif
