#include <string.h>
#include <logger.h>

static char * descriptions[] = {"DEBUG", "INFO", "ERROR", "FATAL"};

char * levelDescription(LOG_LEVEL level) {
    return descriptions[level];
}

int descriptionLevel(char * description) {
    int max = sizeof(descriptions) / sizeof(char *);
    for(int i = 0; i < max; i++)
        if (strcmp(description, descriptions[i]) == 0)
            return i;
            
    return -1;
}
