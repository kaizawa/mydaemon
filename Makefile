CC=gcc
PRODUCTS = mydaemon

all: $(PRODUCTS)

clean:
	rm -f $(PRODUCTS)

mydaemon: mydaemon.c
	$(CC) mydaemon.c -g -o mydaemon

install: mydaemon 
	cp mydaemon /home/kaizawa/tools/bin/

