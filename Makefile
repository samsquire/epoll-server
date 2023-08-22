CC := gcc
CFLAGS += -Wall
CFLAGS += -Wextra
CFLAGS += -g
CFLAGS += -march=native
default: bin/epollserver.o bin/refactored.o bin/selectserver.o bin/server.o bin/selectserver.o bin/epollserver_threaded.o bin/epollserver_multiplexed.o


bin/%.o: %.c
	$(CC) $(CFLAGS) -o $@ $< -I -L map.c -pthread -lbsd ; \
	chmod +x $@
clean:
	rm bin/*.o
