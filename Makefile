PHONY = clean all

CFLAGS = -I src/include/ -Wall -Wextra -pedantic -pedantic-errors -std=c11 -g\
 -D_POSIX_C_SOURCE=200112L -fsanitize=address

GCC_CXXFLAGS = -DMESSAGE='"Compiled with GCC"'
CLANG_CXXFLAGS = -DMESSAGE='"Compiled with Clang"' -Wno-gnu-zero-variadic-macro-arguments
UNKNOWN_CXXFLAGS = -DMESSAGE='"Compiled with an unknown compiler"'

ifeq ($(CC),g++)
  CFLAGS += $(GCC_CXXFLAGS)
else ifeq ($(CC),clang)
  CFLAGS += $(CLANG_CXXFLAGS)
else
  CFLAGS += $(UNKNOWN_CXXFLAGS)
endif

all: httpd client

PROXY_OBJ = src/lib/address.o src/lib/args.o src/lib/buffer.o src/lib/http.o src/lib/logger.o\
 src/lib/selector.o src/lib/pop3_parser.o src/lib/parser/abnf_chars.o src/lib/parser.o\
 src/lib/tcp_utils.o src/lib/udp_utils.o src/lib/statistics.o src/lib/stm.o src/lib/dissector.o\
 src/lib/parser/http_message_parser.o src/lib/parser/http_request_parser.o\
 src/lib/parser/http_response_parser.o src/httpd/main.o src/httpd/monitor.o\
 src/httpd/proxy_stm.o src/httpd/doh_client.o

CLIENT_OBJ = src/lib/client_argc.o src/httpd/httpdctl.o

httpd: $(PROXY_OBJ)
	$(CC) -pthread $(CFLAGS) $(PROXY_OBJ) -o bin/httpd

client: $(CLIENT_OBJ)
	$(CC) -pthread $(CFLAGS) $(CLIENT_OBJ) -o bin/httpdctl

clean:
	rm -rf $(PROXY_OBJ) $(CLIENT_OBJ) bin/httpd bin/httpdctl