.PHONY: compile run


run: prog1
	@./prog1

prog1: prog1.c
	@gcc -Wall prog1.c -o prog1

hide_errors: prog1
	@./prog1 2>/dev/null

clean:
	@rm prog1

