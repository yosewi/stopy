#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <bits/getopt_core.h> // niepotrzebne ale IDE nie widziało optarg


#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))
#define MAXPATH 101

void create_environment(const char *name) {
    struct stat st;
    if (lstat(name, &st) == 0) {
        fprintf(stderr, "etap1: environment %s already exists\n", name);
        exit(EXIT_FAILURE);
    }

    if (mkdir(name, 0777) == -1) {
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

void install_package(const char *env, const char *pkg) {
    char pkg_copy[256];
    strncpy(pkg_copy, pkg, sizeof(pkg_copy));
    pkg_copy[sizeof(pkg_copy)-1] = '\0';

    char *sep = strstr(pkg_copy, "==");
    if (!sep) {
        fprintf(stderr, "invalid package syntax\n");
        fprintf(stderr, "usage: ./sop-venv -c -v <ENVIRONMENT_NAME>\n");
        exit(EXIT_FAILURE);
    }
    *sep = '\0';
    char *pkg_name = pkg_copy;
    char *pkg_ver = sep + 2;

    char path[MAXPATH];
    snprintf(path, sizeof(path), "%s/requirements", env);

    FILE *f = fopen(path, "a+");
    if (!f) {
        ERR("fopen");
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char existing_pkg[256];
        sscanf(line, "%s", existing_pkg);
        if (strcmp(existing_pkg, pkg_name) == 0) {
            fprintf(stderr, "package %s already installed\n", pkg_name);
            fclose(f);
            exit(EXIT_FAILURE);
        }
    }

    fprintf(f, "%s %s\n", pkg_name, pkg_ver);
    fclose(f);

    // Tworzenie pliku pakietu (tylko do odczytu)
    snprintf(path, sizeof(path), "%s/%s", env, pkg_name);
    f = fopen(path, "w");
    if (!f) ERR("fopen");

    fputs("randomdata", f); // przykładowa zawartość
    fclose(f);
    if (chmod(path, 0444) == -1) ERR("chmod");
}

int main(int argc, char **argv) {
    int c;
    char *env_name = NULL;
    char *pkg_to_install = NULL;
    int create_env = 0;

    // Parsowanie argumentów
    while ((c = getopt(argc, argv, "c:v:i:")) != -1) {
        switch (c) {
            case 'c':
                create_env = 1;
                break;
            case 'v':
                env_name = optarg;
                break;
            case 'i':
                pkg_to_install = optarg;
                break;
            default:
                fprintf(stderr, "usage: ./sop-venv -c -v <ENVIRONMENT_NAME>\n");
                exit(EXIT_FAILURE);
        }
    }

    if (create_env) {
        if (!env_name) {
            fprintf(stderr, "usage: ./sop-venv -c -v <ENVIRONMENT_NAME>\n");
            exit(EXIT_FAILURE);
        }
        create_environment(env_name);
    }

    if (pkg_to_install) {
        if (!env_name) {
            fprintf(stderr, "no environment specified\n");
            exit(EXIT_FAILURE);
        }
        install_package(env_name, pkg_to_install);
    }

    return 0;
}
