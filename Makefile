PHONY = clean all

CFLAGS = -I src/include/ -Wall -Wextra -pedantic -pedantic-errors -std=c11 -g\
 -D_POSIX_C_SOURCE=200112L -Wno-unused-function # -fcommon

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

PROXY_OBJ = src/httpd/dissector.o src/httpd/doh_client.o src/httpd/main.o src/httpd/proxy_args.o \
	src/httpd/proxy.o src/httpd/selector.o src/httpd/statistics.o src/lib/address.o src/lib/buffer.o \
	src/lib/http.o src/lib/logger.o src/lib/stm.o src/lib/tcp.o src/lib/udp.o src/lib/parser.o \
	src/monitor/monitor.o src/parsers/abnf_chars.o src/parsers/http_message_parser.o \
	src/parsers/http_request_parser.o src/parsers/http_response_parser.o src/parsers/pop3_parser.o \
	src/stm/doh_handlers.o src/stm/proxy_stm.o src/stm/request_handlers.o src/stm/response_handlers.o

CLIENT_OBJ = src/monitor/client_args.o src/monitor/client.o

all: proxy client

proxy: $(PROXY_OBJ)
	$(CC) -pthread $(CFLAGS) $(PROXY_OBJ) -o bin/proxy

client: $(CLIENT_OBJ)
	$(CC) -pthread $(CFLAGS) $(CLIENT_OBJ) -o bin/client

clean:
	rm -rf $(PROXY_OBJ) $(CLIENT_OBJ) bin/proxy bin/client