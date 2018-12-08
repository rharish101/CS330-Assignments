all:objfs
CC = gcc
CFLAGS  = -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 -D_GNU_SOURCE -g -Wall 
LDFLAGS = -pthread -lfuse
OBJS = objfs.o lib.o objstore.o
OBJS_CACHED = objfs.o lib.o objstore_cached.o

%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@
objstore_cached.o : objstore.c
	$(CC) -c $(CFLAGS) -DCACHE objstore.c -o $@

objfs: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

objfs_cached: $(OBJS_CACHED)
	$(CC) -o $@ $(OBJS_CACHED) $(LDFLAGS)

.Phony: clean
clean:
	rm -f *.o; rm -f objfs objfs_cached;
