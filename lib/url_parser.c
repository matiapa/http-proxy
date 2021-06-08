/* -------------------------------------------------------------------
    Code taken from: https://github.com/jaysonsantos/url-parser-c
    Author: jaysonsantos
    License: The MIT License (MIT)
    Date: 2021-08-06
------------------------------------------------------------------- */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <url_parser.h>

void free_parsed_url(struct url *url_parsed) {
	if (url_parsed->protocol)
		free(url_parsed->protocol);
	if (url_parsed->host)
		free(url_parsed->host);
	if (url_parsed->path)
		free(url_parsed->path);
	if (url_parsed->query_string)
		free(url_parsed->query_string);
}


int parse_url(char *url, struct url *parsed_url) {
	char *local_url = (char *) malloc(sizeof(char) * (strlen(url) + 1));
	char *token;
	char *token_host;
	char *host_port;
	char *host_ip;

	char *token_ptr;
	char *host_token_ptr;

	char *path = NULL;

	// Copy our string
	strcpy(local_url, url);

	token = strtok_r(local_url, ":", &token_ptr);
	parsed_url->protocol = (char *) malloc(sizeof(char) * strlen(token) + 1);
	strcpy(parsed_url->protocol, token);

	// Host:Port
	token = strtok_r(NULL, "/", &token_ptr);
	if (token) {
		host_port = (char *) malloc(sizeof(char) * (strlen(token) + 1));
		strcpy(host_port, token);
	} else {
		host_port = (char *) malloc(sizeof(char) * 1);
		strcpy(host_port, "");
	}

	token_host = strtok_r(host_port, ":", &host_token_ptr);
	parsed_url->host_ip = NULL;
	if (token_host) {
		parsed_url->host = (char *) malloc(sizeof(char) * strlen(token_host) + 1);
		strcpy(parsed_url->host, token_host);
	} else {
		parsed_url->host_exists = -1;
		parsed_url->host = NULL;
	}

	// Port
	token_host = strtok_r(NULL, ":", &host_token_ptr);
	if (token_host)
		parsed_url->port = atoi(token_host);
	else
		parsed_url->port = 0;

	token_host = strtok_r(NULL, ":", &host_token_ptr);
	assert(token_host == NULL);

	token = strtok_r(NULL, "?", &token_ptr);
	parsed_url->path = NULL;
	if (token) {
		path = (char *) realloc(path, sizeof(char) * (strlen(token) + 2));
		strcpy(path, "/");
		strcat(path, token);

		parsed_url->path = (char *) malloc(sizeof(char) * strlen(path) + 1);
		strncpy(parsed_url->path, path, strlen(path));

		free(path);
	} else {
		parsed_url->path = (char *) malloc(sizeof(char) * 2);
		strcpy(parsed_url->path, "/");
	}

	token = strtok_r(NULL, "?", &token_ptr);
	if (token) {
		parsed_url->query_string = (char *) malloc(sizeof(char) * (strlen(token) + 1));
		strncpy(parsed_url->query_string, token, strlen(token));
	} else {
		parsed_url->query_string = NULL;
	}

	token = strtok_r(NULL, "?", &token_ptr);
	assert(token == NULL);

	free(local_url);
	free(host_port);
	return 0;
}