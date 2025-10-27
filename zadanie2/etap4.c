#define _XOPEN_SOURCE 500

#include <dirent.h>
//#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h> // funkcja strrchr
#include <ftw.h> // struct FTW

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))
#define MAX_PATH 101
#define MAXFD 20

int depth;
char *extension;
FILE *buf;

char *get_file_extention(const char *filename){
    char *dot = strrchr(filename, '.');
    if(!dot || dot == filename){
        return "";
    }
    return dot+1;
}

int walk(const char* name, const struct stat *s, int type, struct FTW *f)
{
    if(f->level > depth) return 0;

    if(strcmp(get_file_extention(name), extension) == 0)
    {
        fprintf(buf, "%s %ld\n", name, s->st_size);
    }
    return 0;
}

FILE *make_file(const char *name){
    FILE *f;
    umask(~0777);
    if((f = fopen(name, "w+")) == NULL){
        ERR("fopen");
    }
    return f;
}

int main(int argc, char **argv){
    int c;
    extension = NULL;
    char path[MAX_PATH];
    buf = stdout;
    putenv("L1_OUTPUTFILE=output.txt");
    if(getcwd(path, MAX_PATH) == NULL){
        ERR("getcwd");
    }

    while((c = getopt(argc,argv,"p:e:d:o")) != -1){
        switch(c){
            case 'e':
                extension = optarg;
                break;
            case 'd':
                depth = atoi(optarg);
                break;
            case 'o':
                char *env = getenv("L1_OUTPUTFILE");
                if(env != NULL){
                    buf = make_file(env);
                }
                else{
                    fprintf(stderr, "Cos nie tak z srodowiskowa");
                }
                break;
        }
    }

    optind = 1;

    while((c = getopt(argc,argv,"p:e:d:o")) != -1){
        switch(c){
            case 'p':
                if(chdir(optarg)){
                    ERR("chdir");
                }
                fprintf(buf, "path: %s\n", optarg);
                if(nftw(optarg, walk, MAXFD, FTW_PHYS) == -1){
                    ERR("nftw");
                }
                if(chdir(path)){
                    ERR("chdir");
                }
                break;
        }
    }

    if(buf != stdout)
    {
        if(fclose(buf)){
            ERR("fclose");
        }
    }

    return EXIT_SUCCESS;
}