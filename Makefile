all: lab1-client.c lab1-server.c
	gcc -Wall -std=gnu99 -orembash lab1-client.c
	gcc -Wall -std=gnu99 -orembashd lab1-server.c
	mkdir serverdir

clean:
	rm rembash rembashd
	rm -r serverdir
