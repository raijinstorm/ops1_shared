CC:=gcc
CFLAGS:=-g -O0 -Wall -Wextra
SOURCES:=$(wildcard *.c)
OBJECTS:=$(SOURCES:.c=.o)
EXEC:=prog

.PHONY: all

all: $(EXEC)

$(EXEC): $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $(EXEC)
	@echo Project linked successfully

%.o: %.c
	$(CC) $(CFLAGS) -c  $< -o $@

clean:
	rm $(EXEC) $(OBJECTS)
	@echo Project was cleaned successfully

