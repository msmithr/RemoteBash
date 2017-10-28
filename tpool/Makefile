all:
	gcc -Wall -std=gnu99 -otest tpool-test.c tpool.c -pthread
	gcc -c -std=gnu99 tpool.c && ar -cr libtpool.a tpool.o
	gcc -Wall -std=gnu99 -otesta tpool-test.c libtpool.a -pthread
	gcc -Wall -std=gnu99 -fpic -c tpool.c && gcc -shared -olibtpool.so tpool.o
	gcc -Wall -std=gnu99 -otestso tpool-test.c libtpool.so -pthread

std:
	gcc -Wall -std=gnu99 -otest tpool-test.c tpool.c -pthread
	
static:
	gcc -c -std=gnu99 tpool.c && ar -cr tpool.a tpool.o
	gcc -Wall -std=gnu99 -otesta tpool-test.c tpool.a -pthread

shared:
	gcc -Wall -std=gnu99 -fpic -c tpool.c && gcc -shared -otpool.so tpool.o
	gcc -Wall -std=gnu99 -otestso tpool-test.c tpool.so -pthread

clean:
	rm test testa testso libtpool.so libtpool.a tpool.o 2>/dev/null
