#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THREAD_COUNT 10

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

typedef unsigned int UINT;
typedef struct t{
    pthread_t tid;
} t_t;
void* thread_counter();

void ReadArguments(int argc, char **argv, int *threadCount);

int main(int argc, char **argv){
    int threadCount;
    ReadArguments(argc, argv, &threadCount);
    t_t *watki = (t_t*)malloc(sizeof(t_t) * threadCount);
    if(watki == NULL){
        ERR("malloc");
    }
    for(int i =0;i<threadCount;i++){
        if(pthread_create(&(watki[i].tid), NULL, thread_counter, NULL)){
            ERR("pthread_create");
        }
    }
    for(int i = 0;i<threadCount;i++){
        if(pthread_join(watki[i].tid, NULL)){
            ERR("pthread_join");
        }
    }
    free(watki);
    exit(EXIT_SUCCESS);
}

void ReadArguments(int argc, char **argv, int *threadCount){
    *threadCount = THREAD_COUNT;

    if(argc >= 2){
        *threadCount = atoi(argv[1]);
        if(*threadCount < 1){
            printf("Invalid value for threadCount");
            exit(EXIT_FAILURE);
        }
    }
}

void* thread_counter(){
    printf("*\n");
    return NULL;
}