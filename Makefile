PHONY = clean all

CFLAGS = -I include/ -Wall -Wextra -pedantic -pedantic-errors -std=c11 -g -D_POSIX_C_SOURCE=200112L -Wno-gnu-zero-variadic-macro-arguments

all: httpd

PROXY_OBJ = lib/http.o lib/logger.o lib/doh_client.o httpd/test_doh.o

httpd: $(PROXY_OBJ)
	$(CC) -pthread $(CFLAGS) $(PROXY_OBJ) -o bin/httpd
	rm -rf $(PROXY_OBJ)

clean:
	rm -rf $(PROXY_OBJ) bin/httpd