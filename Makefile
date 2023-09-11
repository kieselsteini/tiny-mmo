# Common defines ---------------------------------------------------------------

CC = cc 
CFLAGS = -std=c99 -O2 -Wall -Wextra

default: client server


# Client -----------------------------------------------------------------------

CLIENT_OBJ = client.o
CLIENT_BIN = client

client: CFLAGS += `sdl2-config --cflags`

client: $(CLIENT_OBJ)
	$(CC) `sdl2-config --libs` -o $(CLIENT_BIN) $(CLIENT_OBJ)


# Server -----------------------------------------------------------------------

SERVER_OBJ = server.o
SERVER_BIN = server

server: $(SERVER_OBJ)
	$(CC) -o $(SERVER_BIN) $(SERVER_OBJ)


# Cleaning ---------------------------------------------------------------------

clean:
	rm -f $(CLIENT_BIN) $(CLIENT_OBJ) $(SERVER_BIN) $(SERVER_OBJ)
