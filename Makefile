PORT=52390
CFLAGS = -DPORT=$(PORT) -g -Wall -std=gnu99

DEPENDENCIES = ftree.h hash.h

all: rcopy_server rcopy_client

rcopy_server: rcopy_server.o ftree.o hash_functions.o
	gcc ${CFLAGS} -o $@ $^

rcopy_client: rcopy_client.o ftree.o hash_functions.o
	gcc ${CFLAGS} -o $@ $^

%.o: %.c ${DEPENDENCIES}
	gcc ${CFLAGS} -c $<

clean:
	rm -f *.o rcopy_server rcopy_client
