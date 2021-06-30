#ifndef __logger_h_
#define __logger_h_
#include <stdlib.h>
#include <stdio.h>
#include <config.h>


typedef enum LOG_LEVEL {DEBUG=0, INFO, ERROR, FATAL} LOG_LEVEL;

char * levelDescription(LOG_LEVEL level);

int descriptionLevel(char * description);

typedef enum colors {BLACK=30, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE} colors;

//-V:log:1001
#define clog(level, color, ...)   { \
	if (level >= proxy_conf.logLevel) { \
		if(level != INFO) \
			fprintf(stderr, "%s: %s:%d, ", levelDescription(level), __FILE__, __LINE__); \
		else \
			fprintf(stderr, "\x1b[34m> \x1b[0m"); \
		fprintf(stderr, "\x1b[%dm", color); \
		fprintf(stderr, ##__VA_ARGS__); \
		fprintf(stderr, "\n\x1b[0m"); \
	} \
	if (level==FATAL) exit(1); \
}

#define log(level, ...) clog(level, WHITE, ##__VA_ARGS__)

#endif
