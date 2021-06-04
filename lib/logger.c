#include "../include/logger.h"
#include <string.h>

LOG_LEVEL current_level = DEBUG;

static char * descriptions[] = {"DEBUG", "INFO", "ERROR", "FATAL"};

void setLogLevel(LOG_LEVEL newLevel) {
	if ( newLevel >= DEBUG && newLevel <= FATAL )
	   current_level = newLevel;
}

char * levelDescription(LOG_LEVEL level) {
    if (level < DEBUG || level > FATAL)
        return "";
    return descriptions[level];
}

int descriptionLevel(char * description) {
    int max = sizeof(descriptions) / sizeof(char *);
    for(int i = 0; i < max; i++)
        if (strcmp(description, descriptions[i]) == 0)
            return i;
            
    return -1;
}