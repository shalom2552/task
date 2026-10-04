CC = gcc
CFLAGS = -Wall -Wextra -pedantic

BIN = task
SRC = task.c

all: $(BIN)

run: $(BIN)
	./$(BIN)

hot: $(BIN)
	ls | entr -c make -s run

clean:
	$(RM) $(BIN)

.PHONY: all run hot clean

