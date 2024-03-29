all: 
	gcc -c -std=gnu99 tpool.c && ar -cr tpool.a tpool.o
	gcc -Wall -std=gnu99 -orembash lab5-client.c
	gcc -Wall -std=gnu99 -orembashd -pthread -lrt lab5-server.c tpool.a
	rm tpool.o

debug:
	gcc -g -c -std=gnu99 tpool.c && ar -cr tpool.a tpool.o
	gcc -g -DDEBUG -Wall -std=gnu99 -orembash lab5-client.c
	gcc -g -DDEBUG -Wall -std=gnu99 -orembashd -pthread -lrt lab5-server.c tpool.a

clean:
	rm rembash rembashd tpool.a
