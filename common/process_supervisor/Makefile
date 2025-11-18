# --- Compiler & Flags -------------------------------------------------------

CC      = gcc
CFLAGS  = -std=c17 -Wall -Wextra -Wpedantic -Werror -O2
LDFLAGS =

# --- Files ------------------------------------------------------------------

SRC     = lab.c
BIN     = lab

# --- Default Target ---------------------------------------------------------

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

# --- Run with example arguments ---------------------------------------------

# Usage:
#     make run ARGS="5 1 3"
run: $(BIN)
	./$(BIN) $(ARGS)

# --- Clean ------------------------------------------------------------------

clean:
	rm -f $(BIN)
	rm -f *.o

# --- Phony Targets -----------------------------------------------------------

.PHONY: all run clean
