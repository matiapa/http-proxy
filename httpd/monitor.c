#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <logger.h>
#include <udp_utils.h>
#include <strings.h>
#include <config.h>

#define REQ_BUFF_SIZE 1024
#define RES_BUFF_SIZE 1024
#define PF proxy_conf

void parse_package(char * command, char * response);

void set_variable(char * command, char * response);

int start_monitor(char *port) {

  int serverSocket = create_udp_server(port);
  if (serverSocket < 0)
    log(FATAL, "Creating server socket: %s ", strerror(errno))

  
  char req[REQ_BUFF_SIZE];
  char res[RES_BUFF_SIZE];

  while (1) {

    memset(req, 0, REQ_BUFF_SIZE);
    memset(req, 0, RES_BUFF_SIZE);

    struct sockaddr_storage clientAddress;
    socklen_t clientAddressSize = sizeof(clientAddress);;
 
    ssize_t recvBytes = uread(serverSocket, req, REQ_BUFF_SIZE, (struct sockaddr *) &clientAddress, &clientAddressSize);
    if (recvBytes < 0) continue;

    
    parse_package(req, res);

    usend(serverSocket, res, strlen(res), (struct sockaddr *) &clientAddress, clientAddressSize);

  }

}



void parse_package(char * command, char * response) {

    for(int i=0; command[i] != 0; i++)
      if(command[i] == '\n'){
        command[i] = 0;
        break;
      }

    if (strncmp(command, "HELP", 4) == 0) {

      sprintf(response,
        "> Available commands:\n"
        ">> SHOW CONFIG:                          Display current values of config variables\n"
        ">> SET variable value:                   Set a config variable value\n"
        ">> HELP:                                 Show this help screen\n\n"

        "> Available config variables:\n"
        ">> maxClients:                           Max allowed clients (up to 1000). Default is 1000.\n"
        ">> connectionTimeout:                    Max inactivity time before disconnection, or -1 to disable it. Default is -1.\n"
        ">> statisticsEnabled:                    Whether to log connection statistics. Default is 1.\n"
        ">> disectorsEnabled:                     Whether to extract plain text credentials. Default is 1.\n"
        ">> viaProxyName:                         Host name to use on RFC 2616 required 'Via' header, up to %d characters. Default is proxy hostname.\n"
        ">> clientBlacklist:                      Comma separated list of client IPs to which service must be denied. Max size of list is %d.\n"
        ">> targetBlacklist:                      Comma separated list of target IPs to which connection must be denied. Max size of list is %d.\n"
        ">> logLevel:                             Minimum log level to display, possible values are [DEBUG, INFO, ERROR, FATAL]. Default is DEBUG.\n",
        
        VIA_PROXY_NAME_SIZE, BLACKLIST_SIZE, BLACKLIST_SIZE
      );

    } else if (strncmp(command, "SHOW CONFIG", 11) == 0) {

      char * format =
        "maxClients: %d\n"
        "connectionTimeout: %d\n"
        "statisticsEnabled: %d\n"
        "disectorsEnabled: %d\n"
        "viaProxyName: %s\n"
        "clientBlacklist: %s\n"
        "targetBlacklist: %s\n"
        "logLevel: %s\n";

      sprintf(response, format, PF.maxClients, PF.connectionTimeout, PF.statisticsEnabled, PF.disectorsEnabled,
        PF.viaProxyName, PF.clientBlacklist, PF.targetBlacklist, levelDescription(PF.logLevel));

    } else if (strncmp(command, "SET", 3) == 0) {
  
      set_variable(command + 4, response);

    } else if (command[0] == 0) {
      
      sprintf(response, "\n");

    } else {

      sprintf(response, "ERROR: Invalid command\n");
        
    }

}


void set_variable(char * command, char * response) {

  char * variable = strtok(command, " ");
  if (variable == NULL) {
    sprintf(response, "ERROR: Missing variable name\n");
    return;
  }

  char * value = strtok(NULL, " ");
  if (value == NULL) {
    sprintf(response, "ERROR: Missing variable value\n");
    return;
  }

  if (strncmp(variable, "maxClients", 10) == 0) {

    int num = atoi(value);
    if (num > 0 && num <= 1000) {
      proxy_conf.maxClients = num;
      sprintf(response, "OK: Max clients set to %d\n", num);
    } else {
      sprintf(response, "ERROR: Value must be a natural n such that 0 < n <= 1000\n");
    }

  } else if (strncmp(variable, "connectionTimeout", 17) == 0) {

    int num = atoi(value);
    if (num > 0 || num == -1) {
      proxy_conf.connectionTimeout = num;
      sprintf(response, "OK: Connection timeout set to %d\n", num);
    } else {
      sprintf(response, "ERROR: Value must be a natural n such that 0 < n or n = -1\n");
    }

  } else if (strncmp(variable, "statisticsEnabled", 17) == 0) {

    int num = atoi(value);
    if (num == 0 || num == 1) {
      proxy_conf.statisticsEnabled = num;
      sprintf(response, "OK: Statistics %s\n", num ? "enabled" : "disabled");
    } else {
      sprintf(response, "ERROR: Value must be a natural n such that n = 0 or n = 1\n");
    }

  } else if (strncmp(variable, "disectorsEnabled", 16) == 0) {

    int num = atoi(value);
    if (num == 0 || num == 1) {
      proxy_conf.disectorsEnabled = num;
      sprintf(response, "OK: Disectors %s\n", num ? "enabled" : "disabled");
    } else {
      sprintf(response, "ERROR: Value must be a natural n such that n = 0 or n = 1\n");
    }

  } else if (strncmp(variable, "viaProxyName", 12) == 0) {

    strncpy(proxy_conf.viaProxyName, value, VIA_PROXY_NAME_SIZE);

    sprintf(response, "OK: Proxy name set to %s\n", proxy_conf.viaProxyName);

  } else if (strncmp(variable, "clientBlacklist", 15) == 0) {

    strncpy(proxy_conf.clientBlacklist, value, BLACKLIST_SIZE);

    sprintf(response, "OK: Client black list set to %s\n", proxy_conf.clientBlacklist);

  } else if (strncmp(variable, "targetBlacklist", 15) == 0) {

    strncpy(proxy_conf.targetBlacklist, value, BLACKLIST_SIZE);

    sprintf(response, "OK: Target black list set to %s\n", proxy_conf.targetBlacklist);

  } else if (strncmp(variable, "logLevel", 8) == 0) {

    int level = descriptionLevel(value);
    if (level >= 0) {
      proxy_conf.logLevel = (LOG_LEVEL) level;
      sprintf(response, "OK: Log level set to %s\n", levelDescription(level));
    } else {
      sprintf(response, "ERROR: Value must be one of [DEBUG, INFO, ERROR, FATAL]\n");
    }

  }

}