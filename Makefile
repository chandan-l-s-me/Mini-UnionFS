CC = gcc

CFLAGS = -Wall -Wextra -O2 -g -D_FILE_OFFSET_BITS=64 $(shell pkg-config fuse --cflags)
LDFLAGS = $(shell pkg-config fuse --libs)

SRC = src/mini_unionfs.c
OBJ = build/mini_unionfs.o

all: mini_unionfs

mini_unionfs: $(OBJ)
	$(CC) $(CFLAGS) -o mini_unionfs $(OBJ) $(LDFLAGS)

build/mini_unionfs.o: $(SRC)
	mkdir -p build
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

clean:
	rm -rf build mini_unionfs