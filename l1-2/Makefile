all: l1-2
l1-2: l1-2.c
	gcc -Wall -fsanitize=address,undefined -o l1-2 l1-2.c
.PHONY: clean all
clean:
	rm l1-2