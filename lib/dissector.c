
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <logger.h>
#include <string.h>
#include <errno.h>
#include <config.h>
#include <dissector.h>
#include <time.h>



void print_credentials(Protocol prot,char * target, int port,char * user, char * pass){

    char * protocolo;
    
    switch (prot)
    {
    case HTTP:
        protocolo="HTTP";
        break;
    
    case POP3:
        protocolo="POP3";
        break;
    }


    char buf[256] = {0};
    
    time_t rawtime = time(NULL);
    struct tm *ptm = localtime(&rawtime);
    

    strftime(buf, 256, "%Y-%m-%dT%TZ", ptm);
    printf("%s  P   %s  %s  %d  %s  %s\n",buf,protocolo,target,port,user,pass);
    

}
