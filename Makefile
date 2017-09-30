all: 
	gcc -Wall -std=gnu99 -orembash -pthread lab3-client.c
	gcc -Wall -std=gnu99 -orembashd -pthread lab3-server.c

debug:
	gcc -DDEBUG -Wall -std=gnu99 -orembash -pthread lab3-client.c
	gcc -DDEBUG -Wall -std=gnu99 -orembashd -pthread lab3-server.c

client:
	gcc -Wall -std=gnu99 -orembash -pthread lab3-client.c

Dclient:
	gcc -DDEBUG -Wall -std=gnu99 -orembash -pthread lab3-client.c

Dserver:
	gcc -DDEBUG -Wall -std=gnu99 -orembashd -pthread lab3-server.c

server:
	gcc -Wall -std=gnu99 -orembashd -pthread lab3-server.c

clean:
	rm rembash rembashd
