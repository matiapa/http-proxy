PHONY = clean all

CFLAGS = -I include/ -Wall -Wextra -pedantic -pedantic-errors -std=c11 -g -D_POSIX_C_SOURCE=200112L -fsanitize=address

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

all: httpd

PROXY_OBJ = lib/address.o lib/args.o lib/buffer.o lib/http.o lib/logger.o lib/selector.o\
 lib/parser.o lib/tcp_utils.o lib/udp_utils.o lib/statistics.o lib/stm.o lib/parser/http_chars.o\
 lib/parser/http_message_parser.o lib/parser/http_request_parser.o\
 httpd/main.o httpd/monitor.o httpd/proxy_stm.o httpd/doh_client.o

httpd: $(PROXY_OBJ)
	$(CC) -pthread $(CFLAGS) $(PROXY_OBJ) -o bin/httpd

clean:
	rm -rf $(PROXY_OBJ) bin/httpd