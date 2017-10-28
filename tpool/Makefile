all:
	gcc -Wall -std=gnu99 -otest tpool-test.c tpool.c -pthread
	gcc -c -std=gnu99 tpool.c && ar -cr libtpool.a tpool.o
	gcc -Wall -std=gnu99 -otesta tpool-test.c libtpool.a -pthread
	gcc -Wall -std=gnu99 -fpic -c tpool.c && gcc -shared -olibtpool.so tpool.o
	gcc -Wall -std=gnu99 -otestso tpool-test.c libtpool.so -pthread

std:
	gcc -Wall -std=gnu99 -otest tpool-test.c tpool.c -pthread
	
static:
	gcc -c -std=gnu99 tpool.c && ar -cr libtpool.a tpool.o
	gcc -Wall -std=gnu99 -otesta tpool-test.c libtpool.a -pthread

shared:
	gcc -Wall -std=gnu99 -fpic -c tpool.c && gcc -shared -olibtpool.so tpool.o
	gcc -Wall -std=gnu99 -otestso tpool-test.c libtpool.so -pthread
	export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:.

clean:
	rm test testa testso libtpool.so libtpool.a tpool.o 2>/dev/null
