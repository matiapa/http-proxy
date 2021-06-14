#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <address.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <ifaddrs.h>



int sockaddr_print(const struct sockaddr * address, char * addrBuffer) {
	void *numericAddress; 
	in_port_t port;

	switch (address->sa_family) {
		case AF_INET:
			numericAddress = &((struct sockaddr_in *) address)->sin_addr;
			port = ntohs(((struct sockaddr_in *) address)->sin_port);
			break;
		case AF_INET6:
			numericAddress = &((struct sockaddr_in6 *) address)->sin6_addr;
			port = ntohs(((struct sockaddr_in6 *) address)->sin6_port);
			break;
		default:
			strcpy(addrBuffer, "[unknown type]");    // Unhandled type
			return 0;
	}

	// Convert binary to printable address
	if (inet_ntop(address->sa_family, numericAddress, addrBuffer, INET6_ADDRSTRLEN) == NULL)
		strcpy(addrBuffer, "[invalid address]"); 
	else if (port != 0)
		sprintf(addrBuffer + strlen(addrBuffer), ":%u", port);

	return 1;
}


int sockaddr_equal(const struct sockaddr * addr1, const struct sockaddr * addr2) {
	if (addr1 == NULL || addr2 == NULL)
		return addr1 == addr2;

	else if (addr1->sa_family != addr2->sa_family)
		return 0;

	else if (addr1->sa_family == AF_INET) {
		struct sockaddr_in *ipv4Addr1 = (struct sockaddr_in *) addr1;
		struct sockaddr_in *ipv4Addr2 = (struct sockaddr_in *) addr2;
		return ipv4Addr1->sin_addr.s_addr == ipv4Addr2->sin_addr.s_addr
			&& ipv4Addr1->sin_port == ipv4Addr2->sin_port;
	
	} else if (addr1->sa_family == AF_INET6) {
		struct sockaddr_in6 *ipv6Addr1 = (struct sockaddr_in6 *) addr1;
		struct sockaddr_in6 *ipv6Addr2 = (struct sockaddr_in6 *) addr2;
		return memcmp(&ipv6Addr1->sin6_addr, &ipv6Addr2->sin6_addr, sizeof(struct in6_addr)) == 0 
			&& ipv6Addr1->sin6_port == ipv6Addr2->sin6_port;
	
	} else
		return 0;
}


int get_machine_fqdn(char * fqdn) {
	// Get unqualified hostname

	char hostname[1024] = {0};
	gethostname(hostname, 1023);

	// Get FQDN if available

	struct addrinfo hints = {0};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;

	struct addrinfo * info;
	int gai_result;
	if ((gai_result = getaddrinfo(hostname, "http", &hints, &info)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_result));
		return -1;
	}

	strcpy(fqdn, info->ai_canonname);

	freeaddrinfo(info);

	return 0;
}


int is_number(const char * str) {
    int i = 0;
    while(*str != '\0') {
        if (*str > '9' || *str < '0') return 0;
        str++;
        i++;
    }
    return i > 0 ? 1 : 0;
}

int parse_url(char * text, struct url * url) {
	char * aux = malloc(strlen(text)+1);
	if (aux == NULL)
		return -1;
		
	strcpy(aux, text);

    memset(url, 0, sizeof(*url));
    char * token = NULL;
    char * rest = aux;
    url->port = 0;
    int flag = 0, num_flag;

    if (rest[0] == '/') { // esta en formato origin
        strcpy(url->path, rest);
		free(aux);
        return 0;
    }

    while (strchr(rest, ':') != NULL && (token = strtok_r(rest, ":", &rest))) {
        num_flag = is_number(token);
        if (!num_flag && !flag) {
            if (strcmp(token, "http") == 0 || strcmp(token, "https") == 0) {
                strcpy(url->protocol, token);
                rest += 2; // por ://
            } else {
                strcpy(url->hostname, token);
                flag = 1;
            }
        } else if (num_flag) {
            url->port = atoi(token);
        } else break;
    }

    token = NULL;
    while (rest!=NULL && strchr(rest, '/') != NULL &&(token = strtok_r(rest, "/", &rest))) {
        num_flag = is_number(token);
        if (!num_flag) {
            if (!flag) {
                strcpy(url->hostname, token);
                flag = 1;
            } else {
                if (rest != NULL){
                    snprintf(url->path, PATH_LENGTH, "/%s/%s", token, rest);
                    rest = NULL;
                } else {
                    snprintf(url->path, PATH_LENGTH, "/%s", token);
                }
            }
        } else {
            url->port = atoi(token);
        }
    }

    if (rest != NULL) {
        num_flag = is_number(rest);
        if (!num_flag) {
            if (flag) {
                snprintf(url->path, PATH_LENGTH, "/%s", rest);
            } else {
                strcpy(url->hostname, rest);
            }
        } else {
            url->port = atoi(rest);
        }
    }

    if (url->port == 0) url->port = 80;

	free(aux);
    return 0;
}

/* returns 1 if it shares the ip with some interface of the proxy if not return 0*/
int isProxy(const struct sockaddr * input){
    
    int isOwn=0;
    struct ifaddrs *ifaddr;
    int family;

    if (getifaddrs(&ifaddr) == -1) {
        // perror("getifaddrs");
        // exit(EXIT_FAILURE);
    }

    /* Walk through linked list, maintaining head pointer so we
        can free list later. */
    for (struct ifaddrs *ifa = ifaddr; ifa != NULL &&!isOwn;ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        family = ifa->ifa_addr->sa_family;
        if (family == AF_INET || family == AF_INET6) {
            if (family == input->sa_family){
                isOwn=sockaddr_equal(ifa->ifa_addr, input);
            }
        }
    }
    freeifaddrs(ifaddr);
    return isOwn;
}