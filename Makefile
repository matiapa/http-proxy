all:
	gcc -g -pthread -I include/ httpd/*.c lib/*.c -o bin/httpd