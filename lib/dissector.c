
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <logger.h>
#include <string.h>
#include <errno.h>
#include <config.h>
#include <dissector.h>
#include <time.h>


// void dump_credentials_to_file(char * str){

//     FILE * fptr;
//     fptr = fopen("./credentials.txt","w+");

//     if(fptr == NULL){
//         log(DEBUG, "Error opening credentials.txt!");   
//         exit(1);
//     }
//     //escribe el string al final del file
//     fseek(fptr, 0, SEEK_END); 
//     fprintf(fptr,"%s \n",str);
//     fclose(fptr);

// }

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

    // time_t rawtime;
    // struct tm * timeinfo; 
    // time(&rawtime);
    // timeinfo = localtime(&rawtime);
    // printf("%s",asctime(timeinfo));
    // 2021-06-12T23:30:53Z


    char buf[256] = {0};
    
    time_t rawtime = time(NULL);
    struct tm *ptm = localtime(&rawtime);
    

    strftime(buf, 256, "%Y-%m-%dT%TZ", ptm);
    printf("%s  P   %s  %s  %d  %s  %s\n",buf,protocolo,target,port,user,pass);
    // printf("%s  P   %s  %s  %d  %s  %s\n",asctime(timeinfo),protocolo,target,port,user,pass);
    

}
