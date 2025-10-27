#define _XOPEN_SOURCE 500
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <ftw.h>
#define MAXPATH 101
#define MAXFLAGS 32

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

int depth = 1;
char *exts[MAXFLAGS];
int ext_count = 0;
FILE *buf;

char *get_ext(const char* filename){
    char *dot = strrchr(filename, '.');
    if(!dot || dot == filename)
        return "";
    return dot + 1;
}

int walk(const char *name, const struct stat *s, int type, struct FTW *f){
    if(f->level > depth)
        return 0;

    for (int i = 0; i < ext_count; i++) {
        if (strcmp(get_ext(name), exts[i]) == 0) {
            fprintf(buf, "%s %ld\n", name, s->st_size);
        }
    }
    return 0;
}

FILE *make_file(char *name){
    FILE *f;
    umask(~0777);
    if((f = fopen(name, "w+")) == NULL){
        ERR("fopen");
    }
    return f;
}

int main(int argc, char **argv) {
    int c;
    char *dirs[MAXFLAGS];
    int dir_count = 0;
    buf = stdout;
    putenv("L1_OUTPUTFILE=output.txt");

    while ((c = getopt(argc, argv, "p:e:d:o")) != -1) {
        switch (c) {
            case 'p':
                dirs[dir_count++] = optarg;
                break;
            case 'e':
                exts[ext_count++] = optarg;
                break;
            case 'd':
                depth = atoi(optarg);
                break;
            case 'o':
                    char *env = getenv("L1_OUTPUTFILE");
                    if(env != NULL){
                        buf=make_file(env);
                    }
                break;
            default:
                fprintf(stderr, "Usage: %s -p <dir1> -p <dir2> ... -e <ext1> -e <ext2> ... -d <depth>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (dir_count == 0) {
        fprintf(stderr, "No directory specified with -p\n");
        exit(EXIT_FAILURE);
    }

    if (ext_count == 0)
        exts[ext_count++] = NULL; // oznacza: wszystkie pliki

    for (int i = 0; i < dir_count; i++) {
        fprintf(buf, "path: %s\n", dirs[i]);
        if (nftw(dirs[i], walk, 10, FTW_PHYS) == -1) {
            ERR("nftw");
        }
    }

    if(buf != stdout){
        if(fclose(buf)){
            ERR("fclose");
        }
    }

    return EXIT_SUCCESS;
}
