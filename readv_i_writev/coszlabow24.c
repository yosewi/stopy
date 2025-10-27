#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>


int main()
{
    int tab[10];  //bufor do zczytania
    struct iovec iov[10]; //struktura iovec
    //for(int i = 0; i< 10; i++) //potrzebne jeśli plik nie istnieje
    //   tab[i] = i*(10-i); //wpisuje do kolejnych tab 9, 16, 21, itd...
    for(int i = 0; i < 10; i++)
    {
        iov[i].iov_base = &tab[i]; //przypisanie bufora
        iov[i].iov_len = 4; //długość inta
    }
    int fd = open("guwno", O_RDWR | O_CREAT, 0666); //tworze plik guwno
    readv(fd,iov,10); //czytanie wartości z struktury tab, bo wczytuje do pliku guwno ioveca, wiec te rzeczy z tablicy beda w guwno
    lseek(fd,0,SEEK_SET); //ustawiam kursor na poczatek, zeby potem read/write sie wykonywal od poczatku
    for(int i = 0; i < 10; i++)
    {
        fprintf(stdout,"i = %d: %d\n", i, tab[i]); //printowanie tego co otrzymaliśmy
        tab[i]++; //zwiekszenie wartosci o 1. whatever
    }
    writev(fd,iov,10); //zapisuje do pliku guwno strukture iovec, tym razem juz zmieniona.
    close(fd);
}