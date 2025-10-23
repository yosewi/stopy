all: prog12
prog12: prog12.c
	gcc -Wall -fsanitize=address,undefined -o prog12 prog12.c
.PHONY: clean all
clean:
	rm prog12