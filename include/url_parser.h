#ifndef URL_PARSER_H
#define URL_PARSER_H

/* -------------------------------------------------------------------
    Code taken from: https://github.com/jaysonsantos/url-parser-c
    Author: jaysonsantos
    License: The MIT License (MIT)
    Date: 2021-08-06
------------------------------------------------------------------- */

typedef struct url {
	char *protocol;
	char *host;
	int port;
	char *path;
	char *query_string;
	int host_exists;
	char *host_ip;
} url;

int parse_url(char *url, struct url *parsed_url);

void free_parsed_url(struct url *url_parsed);

#endif