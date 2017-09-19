all: 
	gcc -Wall -std=gnu99 -orembash lab1-client.c
	gcc -Wall -std=gnu99 -orembashd lab1-server.c

debug:
	gcc -DDEBUG -Wall -std=gnu99 -orembash lab1-client.c
	gcc -DDEBUG -Wall -std=gnu99 -orembashd lab1-server.c

client:
	gcc -Wall -std=gnu99 -orembash lab1-client.c

Dclient:
	gcc -DDEBUG -Wall -std=gnu99 -orembash lab1-client.c

Dserver:
	gcc -DDEBUG -Wall -std=gnu99 -orembashd lab1-server.c

server:
	gcc -Wall -std=gnu99 -orembashd lab1-server.c

clean:
	rm rembash rembashd
