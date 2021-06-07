#include <statistics.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <logger.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

long global_total_connections;
int global_concurent_connections;


char* buffer;
int fd=-1;

void update(int signal_recv);

void initialize_statistics(){
    log(DEBUG, "initialized statistics");
    fd=open("./statistics.txt", O_RDWR | O_CREAT | O_TRUNC, S_IRWXU|S_IRWXG|S_IRWXO);
    global_total_connections=0;
    global_concurent_connections=0;
    buffer=malloc(1024);

    signal(SIGALRM,update);

    alarm(STATISTICS_TIMER);
}

void add_connection(){
    log(DEBUG,"adding connection");
    global_concurent_connections++;
    global_total_connections++;
    log(DEBUG,"connections active %d total %ld",global_concurent_connections,global_total_connections);
}
void remove_conection(){
    global_concurent_connections--;
}


 
void update(int signal_recv){
    log(DEBUG, "statistics update");
    // use appropriate location if you are using MacOS or Linux

    
    sprintf(buffer,"Number of total connections since server start: %ld\n",global_total_connections);
    sprintf(buffer + strlen(buffer),"Number of concurrent connections at alarm: %d\n",global_concurent_connections);
    write(fd,buffer,strlen(buffer));

    log(DEBUG,"errno %d",errno);

    
    // fptr= fdopen(fd, "w+");
    // fptr = fopen("./statistics.txt","w+");

    // if(fptr == NULL){
    //     log(DEBUG, "Error opening statistics.txt!");   
    //     exit(1);
    // }
    // fprintf(fptr,"Number of total connections since server start: %ld\n",global_total_connections);
    // fprintf(fptr,"Number of current concurrent connections: %d\n",global_concurent_connections);
    // fclose(fptr);
    

    alarm(STATISTICS_TIMER);
    signal(SIGALRM,update);

}
 
