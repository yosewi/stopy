#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

int main() {
    int fd = open("input.txt", O_RDONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    struct iovec iov[3];
    char buf1[6];
    char buf2[2];
    char buf3[7];

    iov[0].iov_base = buf1;
    iov[0].iov_len = sizeof(buf1) - 1;
    iov[1].iov_base = buf2;
    iov[1].iov_len = sizeof(buf2) - 1;
    iov[2].iov_base = buf3;
    iov[2].iov_len = sizeof(buf3) - 1;

    ssize_t bytes_read = readv(fd, iov, 3);
    if (bytes_read == -1) {
        perror("readv");
    } else {
        buf1[5] = '\0';  // końcowy znak null dla bufora 1
        buf2[1] = '\0';  // końcowy znak null dla bufora 2
        buf3[6] = '\0';  // chyba nie potrzebne te 3 linijki
        printf("Odczytano %zd bajtów: \"%s\" i \"%s\" i \"%s\"\n", bytes_read, buf1, buf2, buf3);
    }

    close(fd);
    return 0;
}