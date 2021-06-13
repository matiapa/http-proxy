#ifndef DISSECTOR_H_
#define DISSECTOR_H_

typedef enum Protocol{HTTP,POP3}Protocol; 
void print_credentials(Protocol prot,char * target, int port,char * user, char * pass);


#endif
