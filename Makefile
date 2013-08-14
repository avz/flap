CC?=cc
LD=$(CC)

PROJECT=flap

OBJS=ServerClient.o ServerClientsPool.o Server.o main.o
VPATH=src

CFLAGS?=-O2

build: $(PROJECT)

$(PROJECT): $(OBJS)
	$(LD) -lc -pthread $(LDFLAGS) $(OBJS) -o "$(PROJECT)"

.c.o:
	$(CC) -ansi -pedantic -c -g -Wall -Wconversion -Werror -pthread -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 $(CFLAGS) src/$*.c

clean:
	rm -f *.o "$(PROJECT)"

install: build
	install "$(PROJECT)" "$(PREFIX)/bin"

test:
	true
