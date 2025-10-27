all: readv writev coszlabow24
readv: readv.c
	gcc -Wall -fsanitize=address,undefined -o readv readv.c
writev: writev.c
	gcc -Wall -fsanitize=address,undefined -o writev writev.c
coszlabow24: coszlabow24.c
	gcc -Wall -fsanitize=address,undefined -o coszlabow24 coszlabow24.c
.PHONY: clean all
clean:
	rm readv
	rm writev
	rm coszlabow24