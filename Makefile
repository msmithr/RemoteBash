all: 
	gcc -Wall -std=gnu99 -orembash lab1-client.c
	gcc -Wall -std=gnu99 -orembashd lab1-server.c

client:
	gcc -Wall -std=gnu99 -orembash lab1-client.c

server:
	gcc -Wall -std=gnu99 -orembashd lab1-server.c

clean:
	rm rembash rembashd
