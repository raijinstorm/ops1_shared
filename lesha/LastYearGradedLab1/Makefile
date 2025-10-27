CC:=gcc
C_FLAGS:=-Wall -g -O0
SOURCES:=$(wildcard *.c)
OBJECTS:=$(SOURCES:.c=.o)
PROG:=prog


all: $(OBJECTS)
	$(CC) $(OBJECTS) -o $(PROG)

%.o:%.c
	$(CC) $(C_FLAGS) -c $< -o $@

clean:
	@rm $(PROG)
	@rm -f $(OBJECTS)