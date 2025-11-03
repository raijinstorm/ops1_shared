CC := gcc
C_FLAGS := -g -Wall
SOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:.c=.o)
EXEC := prog

all: $(EXEC)

compile: all
	@echo "Compilation finished"

run: all
	./$(EXEC)

$(EXEC): $(OBJECTS)
	$(CC) $(C_FLAGS) $(OBJECTS) -o $(EXEC)

%.o: %.c
	$(CC) $(C_FLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(EXEC)
