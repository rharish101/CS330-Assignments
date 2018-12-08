CC = gcc
LDFLAGS = -pthread
SRCS = $(wildcard *.c)
PROGS = $(patsubst %.c,%,$(SRCS))
OPS = $(patsubst %.c,%.o,$(SRCS))
CFLAGS = -DDEBUG_PRINT9

TARS = $(wildcard *.tgz)
TAR_FILES = $(patsubst %.tgz,%,$(TARS))

all: $(PROGS) $(TAR_FILES)

lib/%.o : lib/%.c
	$(CC) -c $(CFLAGS) $< -o $@

%.o : %.c Makefile
	$(CC) -c $(CFLAGS) $< -iquote lib -o $@
all_o : $(OPS) lib/kvstore.o
% : %.o lib/kvstore.o
	$(CC) -o $@ $^ $(LDFLAGS)

% : %.tgz
	tar -xmzf $<

.Phony: all clean
clean:
	rm -f *.o; rm -f lib/*.o; rm -f $(PROGS);
