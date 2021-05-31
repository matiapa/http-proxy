all:
	gcc -D_DEBUG -std=c11 -g httpd/main.c lib/*.c -o bin/main