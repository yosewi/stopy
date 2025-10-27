#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <bits/getopt_core.h> // niepotrzebne ale IDE nie widziało optarg
#include <time.h>
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
    snprintf(path, sizeof(path), "%s/requirements", name);

    FILE *f = fopen(path, "w");
    if (!f) {
        rmdir(name);
        ERR("fopen");
    }
    fclose(f);
}

void install_package(const char *env, const char *arg){
    struct stat st;
    char path[MAXPATH];
    snprintf(path, sizeof(path), "%s/requirements", env); //tworzy lancuch path z danym tekstem

    if(lstat(env, &st) == -1 || !S_ISDIR(st.st_mode)){
        fprintf(stderr, "environment does not exist\n");
        usage();
    }

    char *pkg = strdup(arg); //tworzy kopie lancucha znakow arg
    char *eq = strstr(pkg, "=="); //wyszukuje == w lancuchu pkg
    if(!eq){
        fprintf(stderr, "invalid package syntax\n");
        free(pkg);
        usage();
    }
    *eq = '\0';
    char *version = eq + 2;

    FILE *f = fopen(path, "w+");
    if(!f){
        ERR("fopen");
    }

    char line[256];
    while(fgets(line, sizeof(line), f)){ //zapisuje tekst z f do line
        char name[128];
        if(sscanf(line, "%127s", name) == 1){ //wczytuje dane z line do name
            if(strcmp(name, pkg) == 0){ //jesli name i pkg sa takie same to dzieje sie to:
                fprintf(stderr, "sop-venv: package '%s' already installed\n", pkg);
                fclose(f);
                free(pkg);
                exit(EXIT_FAILURE);
            }
        }
    }
    fclose(f);
    f = fopen(path, "a");
    if (!f)
        ERR("fopen append");

    fprintf(f, "%s %s\n", pkg, version);
    fclose(f);

    // utwórz plik z losową zawartością
    char pkg_path[MAXPATH];
    snprintf(pkg_path, sizeof(pkg_path), "%s/%s", env, pkg); //tworzy lancuch z podanym tekstem

    FILE *pf = fopen(pkg_path, "w");
    if (!pf)
        ERR("fopen package");

    srand(time(NULL));
    int len = 20 + rand() % 30; // 20–50 znaków
    for (int i = 0; i < len; i++) {
        char c = 'a' + rand() % 26;
        fputc(c, pf);
    }
    fputc('\n', pf);
    fclose(pf);

    // nadaj uprawnienia 0444 (tylko do odczytu)
    if (chmod(pkg_path, 0444) == -1)
        ERR("chmod");

    printf("Installed package '%s' version '%s' in '%s'\n", pkg, version, env);
    free(pkg);
}

int main(int argc, char **argv){
    int c;
    int i=0;
    char *env = NULL;
    char *pkg = NULL;
    while((c = getopt(argc, argv, "cv:i:")) != -1){
        switch(c){
            case 'c':
                i = 1;
                break;
            case 'v':
                env = optarg;
                break;
            case 'i':
                pkg = optarg;
                break;
            default:
                usage();
        }
    }
    if(i && env){
        create_environment(env);
    }
    else if(env && pkg){
        install_package(env, pkg);
    }
    else{
        usage();
    }
    return EXIT_SUCCESS;
}