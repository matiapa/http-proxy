#ifndef DISSECTOR_H_
#define DISSECTOR_H_

#include <http.h>

typedef enum Protocol{HTTP,POP3}Protocol; 
void print_credentials(Protocol prot,char * target, int port,char * user, char * pass);

void print_Access(char * origin,int origin_port,char * target, methods method,int status_code);


#endif
