#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <bits/getopt_core.h> // niepotrzebne ale IDE nie widziało optarg
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(char *pname){
    fprintf(stderr, "USAGE:%s -n Name ip OCTAL -s SIZE\n", pname);
    exit(EXIT_FAILURE);
}

void make_file(char *name, ssize_t size, mode_t perms, int percent){
    FILE *f;
    umask(~perms & 0777);
    if((f = fopen(name, "w+")) == NULL){
        ERR("fopen");
    }
    for(int i=0; i<(size * percent) / 100;i++){
        if(fseek(f, rand() % size, SEEK_SET)){
            ERR("fseek");
        }
        fprintf(f, "%c", 'A' + (i % ('Z' - 'A' +1)));
    }
    if(fclose(f)){
        ERR("fclose");
    }
}

int main(int argc, char **argv){
    int c;
    char *name = NULL;
    mode_t perms = -1;
    ssize_t size = -1;
    while((c = getopt(argc, argv, "p:n:s:")) != -1){
        switch(c){
            case 'p':
                perms = strtol(optarg, (char **)NULL, 8);
                break;
            case 's':
                size = strtol(optarg, (char **)NULL, 10);
                break;
            case 'n':
                name = optarg;
                break;
            case '?':
            default:
                usage(argv[0]);
        }
    }
    if((name == NULL) || (perms == (mode_t)-1) || (size == -1)){
        usage(argv[0]);
    }
    if(unlink(name) && errno != ENOENT){
        ERR("unlink");
    }
    srand(time(NULL));
    make_file(name, size, perms, 10);
    return EXIT_SUCCESS;
}