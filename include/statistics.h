
#ifndef STATISTICS_H
#define STATISTICS_H

#define STATISTICS_TIMER 2
extern long global_total_connections;
extern int global_concurent_connections;


void initialize_statistics();
void add_connection();
void remove_conection();

#endif

