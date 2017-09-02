all: 
	gcc -Wall -std=gnu99 -orembash lab1-client.c
	gcc -Wall -std=gnu99 -orembashd lab1-server.c

debug:
	gcc -Wall -std=gnu99 -DDEBUG -orembash lab1-client.c
	gcc -Wall -std=gnu99 -DDEBUG -orembashd lab1-server.c

client:
	gcc -Wall -std=gnu99 -orembash lab1-client.c

Dclient:
	gcc -Wall -std=gnu99 -DDEBUG -orembash lab1-client.c

server:
	gcc -Wall -std=gnu99 -orembashd lab1-server.c

Dserver:
	gcc -Wall -std=gnu99 -DDEBUG -orembashd lab1-server.c

clean:
	rm rembash rembashd
