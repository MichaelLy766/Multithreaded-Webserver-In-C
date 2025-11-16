CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -pthread
LDFLAGS =

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

BIN_DIR = bin
TARGET = $(BIN_DIR)/server

all: $(TARGET)

$(TARGET): $(OBJ) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET)

.PHONY: all clean
