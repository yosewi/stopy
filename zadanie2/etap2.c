#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <bits/getopt_core.h> // niepotrzebne ale IDE nie widziało optarg
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#define MAXPATH 101

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void scan_dir(const char *extension){
    DIR *d;
    struct dirent *dp;
    struct stat filestat;
    if((d = opendir(".")) == NULL){
        ERR("opendir");
    }
    do{
        if((dp = readdir(d)) != NULL){
            if(lstat(dp->d_name, &filestat)){
                ERR("lstat");
            }
            if(extension != NULL){
                const char *dot = strrchr(dp->d_name, '.'); //zwraca wskaznik do ostatniego wystapienia znaku
                if(!dot || strcmp(dot + 1, extension) != 0){ //porownoje to po tym znaku . z extension
                    continue;
                }
            }
            printf("%s %ld\n", dp->d_name, filestat.st_size);
        }
    }while(dp != NULL);
    if(closedir(d)){
        ERR("closedir");
    }
}

int main(int argc, char **argv){
    int c;
    char *extension = NULL;
    char path[MAXPATH];
    if(getcwd(path, MAXPATH) == NULL){
        ERR("getcwd");
    }

    while((c = getopt(argc,argv,"p:e:")) != -1){
        switch(c){
            case 'e':
                extension = optarg;
                break;
        }
    }

    optind = 1;

    while((c = getopt(argc,argv,"p:e:")) != -1){
        switch(c){
            case 'p':
                if(chdir(optarg)){
                    ERR("chdir");
                }
                printf("path: %s\n", optarg);
                scan_dir(extension);
                if(chdir(path)){
                    ERR("chdir");
                }
                break;
        }
    }
    return EXIT_SUCCESS;
}