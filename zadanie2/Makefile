all: etap1 etap2 etap3 etap4 zadanie2 zadanie2r
etap1: etap1.c
	gcc -Wall -fsanitize=address,undefined -o etap1 etap1.c
etap2: etap2.c
	gcc -Wall -fsanitize=address,undefined -o etap2 etap2.c
etap3: etap3.c
	gcc -Wall -fsanitize=address,undefined -o etap3 etap3.c
etap4: etap4.c
	gcc -Wall -fsanitize=address,undefined -o etap4 etap4.c
zadanie2: zadanie2.c
	gcc -Wall -fsanitize=address,undefined -o zadanie2 zadanie2.c
zadanie2r: zadanie2r.c
	gcc -Wall -fsanitize=address,undefined -o zadanie2r zadanie2r.c
.PHONY: clean all
clean:
	rm etap1
	rm etap2
	rm etap3
	rm etap4
	rm zadanie2
	rm zadanie2r