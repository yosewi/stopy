#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <bits/getopt_core.h> // niepotrzebne ale IDE nie widziało optarg
#define MAXPATH 256

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage() {
    fprintf(stderr, "usage: ./sop-venv -c -v <ENVIRONMENT_NAME>\n");
    exit(EXIT_FAILURE);
}

void create_environment(const char *name){
    struct stat st;
    if(lstat(name, &st) == 0){
        fprintf(stderr, "etap1: environment %s already exists\n", name);
        ERR("lstat");
    }
    if(mkdir(name, 0777) == -1){
        ERR("mkdir");
    }
    char path[MAXPATH];
    snprintf(path, sizeof(path), "%s/requirements", name); //zapisuje do lancucha path tekst

    FILE *f = fopen(path, "w"); //otwiera plik w folderze name o nazwie requirements
    if (!f) {
        rmdir(name);
        ERR("fopen");
    }
    fclose(f);
}

int main(int argc, char **argv){
    int c;
    int i=0;
    char *env = NULL;
    while((c = getopt(argc, argv, "c:v:")) != -1){
        switch(c){
            case 'c':
                env = optarg;
                i = 1;
                break;
            case 'v':
                env = optarg;
                break;
            default:
                usage();
        }
    }
    if(!i || !env){
        usage();
    }
    create_environment(env);
    return EXIT_SUCCESS;
}