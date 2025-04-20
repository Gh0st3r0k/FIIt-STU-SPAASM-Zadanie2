# Compiler and flags
CC = gcc
CFLAGS = -Wall

# Project name
TARGET = spaasm

# Source files
SRCS = main.c server.c client.c shell.c prompt.c

all: $(TARGET)

# Build default target
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

# Clean build files
clean:
	rm -f $(TARGET)