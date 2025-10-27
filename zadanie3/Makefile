all: etap1 etap2 etap3
etap1: etap1.c
	gcc -Wall -fsanitize=address,undefined -o etap1 etap1.c
etap2: etap2.c
	gcc -Wall -fsanitize=address,undefined -o etap2 etap2.c
etap3: etap3.c
	gcc -Wall -fsanitize=address,undefined -o etap3 etap3.c
.PHONY: clean all
clean:
	rm etap1
	rm etap2
	rm etap3