all:
	gcc -D_DEBUG -g httpd/main.c lib/*.c -o bin/main