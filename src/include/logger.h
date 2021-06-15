#ifndef __logger_h_
#define __logger_h_
#include <stdlib.h>
#include <stdio.h>
#include <config.h>


typedef enum {DEBUG=0, INFO, ERROR, FATAL} LOG_LEVEL;

char * levelDescription(LOG_LEVEL level);

int descriptionLevel(char * description);

//-V:log:1001
#define log(level, ...)   { \
	if (level >= proxy_conf.logLevel) { \
		if(level != INFO) \
			fprintf(stderr, "%s: %s:%d, ", levelDescription(level), __FILE__, __LINE__); \
		else \
			fprintf(stderr, "\x1b[1;34m> \x1b[1;0m"); \
		fprintf(stderr, ##__VA_ARGS__); \
		fprintf(stderr, "\n"); \
	} \
	if (level==FATAL) exit(1); \
}

#endif
