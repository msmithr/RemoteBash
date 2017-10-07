all: 
	gcc -Wall -std=gnu99 -orembash lab3-client.c
	gcc -Wall -std=gnu99 -orembashd -pthread -lrt lab3-server.c

debug:
	gcc -DDEBUG -Wall -std=gnu99 -orembash lab3-client.c
	gcc -DDEBUG -Wall -std=gnu99 -orembashd -pthread -lrt lab3-server.c

client:
	gcc -Wall -std=gnu99 -orembash lab3-client.c

Dclient:
	gcc -DDEBUG -Wall -std=gnu99 -orembash lab3-client.c

Dserver:
	gcc -DDEBUG -Wall -std=gnu99 -orembashd -pthread -lrt lab3-server.c

server:
	gcc -Wall -std=gnu99 -orembashd -pthread -lrt lab3-server.c

clean:
	rm rembash* rembashd*

bad:
	gcc -DDEBUG -Wall -std=gnu99 -orembash-badsecret lab3-client-badsecret.c
	gcc -DDEBUG -Wall -std=gnu99 -orembash-terminate1 lab3-client-terminate1.c
	gcc -DDEBUG -Wall -std=gnu99 -orembash-terminate2 lab3-client-terminate2.c
	gcc -DDEBUG -Wall -std=gnu99 -orembash-terminate3 lab3-client-terminate3.c
	gcc -DDEBUG -Wall -std=gnu99 -orembash-terminate4 lab3-client-terminate4.c
	gcc -DDEBUG -Wall -std=gnu99 -orembash-noresponse lab3-client-noresponse.c


