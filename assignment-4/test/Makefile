all:tester
CC = gcc
LDFLAGS = -pthread
OBJS = kvstore.o create.o find_and_read.o

%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@
put: $(OBJS)
	$(CC) -o $@ create.o kvstore.o $(LDFLAGS)
get: $(OBJS)
	$(CC) -o $@ find_and_read.o kvstore.o $(LDFLAGS)
tester: kvstore.o test.o
	$(CC) -o $@ test.o kvstore.o $(LDFLAGS)

.Phony: clean
clean:
	rm -f *.o; rm -f put; rm -f get; rm -f tester;
