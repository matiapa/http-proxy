PHONY = clean all

CFLAGS = -I include/ -Wall -Wextra -pedantic -pedantic-errors -Wno-gnu-zero-variadic-macro-arguments -std=c11 -g -D_POSIX_C_SOURCE=200112L -D_DEBUG

all: httpd

PROXY_OBJ = lib/address.o lib/args.o lib/buffer.o lib/client.o lib/http_request_factory.o lib/io.o lib/logger.o lib/selector.o lib/server.o lib/doh_client.o httpd/test_doh.o

httpd: $(PROXY_OBJ)
	$(CC) -pthread $(CFLAGS) $(PROXY_OBJ) -o bin/httpd
	rm -rf $(PROXY_OBJ)

clean:
	rm -rf $(PROXY_OBJ) bin/httpd