CC=gcc
CFLAGS=-Wall -fsanitize=address,undefined
LDFLAGS=-fsanitize=address,undefined

FILE_NAME=prog5
C_FILE=$(FILE_NAME:=.c)

.PHONY: compile run

run: $(FILE_NAME)
	@./$(FILE_NAME)

prog1: $(C_FILE)
	@$(CC) $(CFLAGS) $(C_FILE) -o $(FILE_NAME)

hide_errors: $(FILE_NAME)
	@./$(FILE_NAME) 2>/dev/null

clean:
	@rm $(FILE_NAME)

