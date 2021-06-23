#ifndef MONITOR_H
#define MONITOR_H

#include <selector.h>

void * start_monitor(void * port);

void handle_read_monitor(struct selector_key *key);

#endif 
