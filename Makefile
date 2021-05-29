.PHONY=clean all

all: httpd
clean:	
	- rm -f *.o httpd

COMMON =  logger.o util.o
httpd:      httpd.o    $(COMMON)
