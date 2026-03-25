CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
FUSE_CFLAGS = $(shell pkg-config --cflags fuse)
FUSE_LIBS = $(shell pkg-config --libs fuse)

INCLUDE_DIR = include
SRC_DIR = src
BUILD_DIR = build

TARGET = mini_unionfs
SOURCES = $(SRC_DIR)/mini_unionfs.c
OBJECTS = $(BUILD_DIR)/mini_unionfs.o

.PHONY: all clean install uninstall help test

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(FUSE_CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(FUSE_CFLAGS) -o $@ $^ $(FUSE_LIBS)
	@echo "Build successful: $(TARGET)"

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
	@echo "Cleaned build artifacts"

test: $(TARGET)
	@bash tests/test_unionfs.sh

help:
	@echo "Mini-UnionFS Makefile"
	@echo "Usage:"
	@echo "  make                - Build the project"
	@echo "  make clean          - Remove build artifacts"
	@echo "  make test           - Run test suite"
	@echo "  make help           - Show this help message"

.DEFAULT_GOAL := all
