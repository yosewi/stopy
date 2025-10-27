#define _XOPEN_SOURCE 500
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <bits/getopt_core.h> // niepotrzebne ale IDE nie widziało optarg
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <ftw.h>
#define MAXPATH 101
#define MAXFLAGS 32

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

int depth;
char *extension;

char *get_ext(const char* filename){
    char *dot = strrchr(filename, '.');
    if(!dot || dot == filename){
        return "";
    }
    return dot + 1;
}

void scan_dir(char *dir, char *ext){
    DIR *d;
    struct dirent *dp;
    struct stat filestat;
    char path[MAXPATH];
    if(getcwd(path, MAXPATH) == NULL){
        ERR("getcwd");
    }
    if(chdir(dir)){
        ERR("chdir");
    }
    if((d = opendir(".")) == NULL){
        ERR("opendir");
    }
    fprintf(stdout, "path: %s\n", dir);
    do{
        if((dp = readdir(d)) != NULL){
            if(lstat(dp->d_name, &filestat)){
                ERR("lstat");
            }
            if(ext != NULL){
                if(strcmp(get_ext(dp->d_name), ext) != 0){
                    continue;
                }
            }
            fprintf(stdout, "%s %ld\n", dp->d_name, filestat.st_size);
        }
    }while(dp!=NULL);
    if(closedir(d)){
        ERR("closedir");
    }
    if(chdir(path)){
        ERR("chdir");
    }
}

int main(int argc, char **argv) {
    int c;
    char *ext[MAXFLAGS];
    char *dirs[MAXFLAGS];
    int ext_count = 0, dir_count = 0;
    // Parsowanie argumentów
    while ((c = getopt(argc, argv, "p:e:")) != -1) {
        switch (c) {
            case 'p':
                dirs[dir_count++] = optarg;  // Przechowuj katalogi
                break;
            case 'e':
                ext[ext_count++] = optarg;  // Przechowuj rozszerzenia
                break;
            default:
                fprintf(stderr, "Usage: %s -p <dir1> -p <dir2> ... -e <ext1> -e <ext2> ...\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Sprawdzamy, czy przynajmniej jeden katalog został podany
    if (dir_count == 0) {
        fprintf(stderr, "No directory specified with -p\n");
        exit(EXIT_FAILURE);
    }

    // Jeśli nie podano rozszerzeń, skanowanie bez rozszerzenia
    if (ext_count == 0) {
        ext[ext_count++] = NULL;  // NULL oznacza brak filtra rozszerzenia
    }

    // Unikamy przetwarzania tych samych katalogów/rozszerzeń wielokrotnie
    // Iterujemy przez katalogi
    for (int i = 0; i < dir_count; i++) {
        // Iterujemy przez rozszerzenia
        for (int j = 0; j < ext_count; j++) {
            scan_dir(dirs[i], ext[j]);
        }
    }

    return EXIT_SUCCESS;
}