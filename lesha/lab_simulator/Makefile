CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -pedantic -std=c11
TARGET  = prog
SRC     = prog.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	@echo "Usage: make run ARGS=\"p t probabilities...\""
	@./$(TARGET) $(ARGS)

clean:
	rm -f $(TARGET)
