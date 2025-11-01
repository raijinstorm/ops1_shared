all: prog1 prog2 prog3
	@echo mishaloh
prog1: prog1.c
	gcc -Wall -fsanitize=address,undefined -o prog1 prog1.c
prog2: prog2.c
	gcc -Wall -fsanitize=address,undefined -o prog2 prog2.c
prog3: prog3.c
	gcc -Wall -fsanitize=address,undefined -o prog3 prog3.c
.PHONY: clean all
clean:
	rm prog1 prog2 prog3
	@echo mishaloh
