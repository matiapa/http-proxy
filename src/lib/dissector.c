
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <logger.h>
#include <string.h>
#include <errno.h>
#include <config.h>
#include <dissector.h>
#include <time.h>
#include <http.h>



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

void print_Access(char * origin,int origin_port,char * target, methods method,int status_code){
    
    char buf[256] = {0};
    
    time_t rawtime = time(NULL);
    struct tm *ptm = localtime(&rawtime);
    

    strftime(buf, 256, "%Y-%m-%dT%TZ", ptm);
    printf("%s  A   %s  %d  %s  %s  %d\n",buf,origin,origin_port,methods_strings[method-1],target,status_code);
}
