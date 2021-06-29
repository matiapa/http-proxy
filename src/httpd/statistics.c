#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <logger.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <config.h>
#include <time.h>
#include <arpa/inet.h>
#include <statistics.h>


long global_total_connections=0;
int global_concurent_connections=0;

long total_bytes_sent=0;
long total_bytes_recieved=0;

char* buffer;
int fd=-1;

void update(int signal_recv);

void initialize_statistics(){
    if (proxy_conf.statisticsFrequency < 0)
        return;

    struct stat st;
    if (stat("./logs", &st) == -1)
        mkdir("./logs", 0700);

    log(DEBUG, "initialized statistics");
    fd=open("./logs/statistics.txt", O_RDWR | O_CREAT | O_TRUNC, S_IRWXU|S_IRWXG|S_IRWXO);
    global_total_connections=0;
    global_concurent_connections=0;
    total_bytes_sent=0;
    total_bytes_recieved=0;
    buffer=malloc(1024);

    signal(SIGALRM,update);

    // alarm(proxy_conf);
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

#pragma GCC diagnostic ignored "-Wunused-parameter"
void update(int signal_recv){
    log(DEBUG, "statistics update");
    log(DEBUG,"total connections: %ld, current connections :%d, total bytes sent: %ld, total bytes recieved: %ld",
    global_total_connections,global_concurent_connections,total_bytes_sent,total_bytes_recieved);

    struct stat st;
    if (stat("./logs", &st) == -1)
        mkdir("./logs", 0700);

    FILE * fptr;
    fptr = fopen("./logs/statistics.txt","w+");

    if(fptr == NULL){
        log(DEBUG, "Error opening statistics.txt!");   
        exit(1);
    }
    time_t rawtime;
    struct tm * timeinfo; 
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    fprintf(fptr,"time and date of last statistics backup: %s", asctime(timeinfo) );
    fprintf(fptr,"Number of total connections since server start: %lu\n",global_total_connections);
    fprintf(fptr,"Number of current concurrent connections: %d\n",global_concurent_connections);
    fprintf(fptr,"Number of total bytes sent since server start: %lu\n",total_bytes_sent);
    fprintf(fptr,"Number of total bytes recieved since server start: %lu\n\n",total_bytes_recieved);
    fclose(fptr);

    alarm(proxy_conf.statisticsFrequency);
    signal(SIGALRM,update);

}
 
void add_sent_bytes(int bytes){
    total_bytes_sent+=bytes;
}

void add_bytes_recieved(int bytes){
    total_bytes_recieved+=bytes;
}

void force_update(){
    signal(SIGALRM,update);
}

statistics * get_statistics(statistics * stats){
    force_update();
    stats->current_connections=global_concurent_connections;
    stats->total_connections=global_total_connections;
    stats->total_recieved=total_bytes_recieved;
    stats->total_sent=total_bytes_sent;

    return stats;
}


void log_client_access(int client_socket, char * url) {

    struct sockaddr_in address;
    int addrlen = sizeof(struct sockaddr_in);
    getpeername(client_socket, (struct sockaddr*) &address, (socklen_t*) &addrlen);

    time_t rawtime; time(&rawtime);
    struct tm * timeinfo = localtime(&rawtime);
    char * date = asctime(timeinfo);
    date[strlen(date)-2] = 0;   // Remove /n

    struct stat st;
    if (stat("./logs", &st) == -1)
        mkdir("./logs", 0700);

    int fd = open("./logs/access.txt", O_RDWR | O_CREAT | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);
    if (fd < 0) {
        log(ERROR, "Failed to open access.txt file with ERRNO %s", strerror(errno));
    }

    char buffer[1024];
    int written = sprintf(buffer, "%s: %s accesed %s\n", date, inet_ntoa(address.sin_addr), url);

    write(fd, buffer, written);

    close(fd);

}
