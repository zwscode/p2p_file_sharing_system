.DEFAULT_GOAL := all
CC = g++
CXXFLAGS=-g -std=c++11

PEER_OBJS = peer_exe.o peer_client.o peer_file_client.o peer_file_server.o tcp_server.o defines.o peer_server.o

SERVER_OBJS = register_server.o tcp_server.o defines.o

all: peer register_server

register_server: $(SERVER_OBJS)
	$(CC) $(SERVER_OBJS) -o register_server

peer: $(PEER_OBJS)
	$(CC) $(PEER_OBJS) -o peer

test: test.o defines.o
	$(CC) test.o defines.o -o test

defines.o: defines.cpp defines.h
	$(CC) -c $(CXXFLAGS) defines.cpp -o defines.o

%.o: %.cpp
	$(CC) -c $(CXXFLAGS) $< -o $@

.PHONY: clean

clean:
	rm -f *.o *.bin peer test register_server