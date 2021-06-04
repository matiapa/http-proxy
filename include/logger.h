#ifndef __logger_h_
#define __logger_h_
#include <stdlib.h>
#include <stdio.h>

typedef enum {DEBUG=0, INFO, ERROR, FATAL} LOG_LEVEL;

extern LOG_LEVEL current_level;

void setLogLevel(LOG_LEVEL newLevel);

char * levelDescription(LOG_LEVEL level);

int descriptionLevel(char * description);

#define log(level, ...)   { \
	if (level >= current_level) { \
		fprintf(stderr, "%s: %s:%d, ", levelDescription(level), __FILE__, __LINE__); \
		fprintf(stderr, ##__VA_ARGS__); \
		fprintf(stderr,"\n"); \
	} \
	if (level==FATAL) exit(1); \
}

#endif