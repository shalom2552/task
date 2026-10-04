CC = gcc
CFLAGS = -Wall -Wextra -pedantic

BIN = todo
SRC = todo.c

all: $(BIN)

run: $(BIN)
	./$(BIN)

hot: $(BIN)
	ls | entr -c make -s run

clean:
	$(RM) $(BIN)

.PHONY: all run hot clean

