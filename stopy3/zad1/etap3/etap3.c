#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#define THREAD_COUNT 10

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

typedef struct timespec timespec_t;
typedef unsigned int UINT;
typedef struct t{
    pthread_t tid;
    UINT seed;
    int M;
    int* L;
} t_t;
void* thread_counter();
void msleep(UINT milisec);

void ReadArguments(int argc, char **argv, int *threadCount);

int main(int argc, char **argv){
    int threadCount;
    int L = 1;
    ReadArguments(argc, argv, &threadCount);
    t_t *watki = (t_t*)malloc(sizeof(t_t) * threadCount);
    if(watki == NULL){
        ERR("malloc");
    }
    srand(time(NULL));
    for(int i = 0;i<threadCount;i++){
        watki[i].seed = rand();
        watki[i].L = &L;
        watki[i].M = rand_r(&watki[i].seed) % 99 + 2;
    }
    for(int i =0;i<threadCount;i++){
        if(pthread_create(&(watki[i].tid), NULL, thread_counter, &watki[i])){
            ERR("pthread_create");
        }
    }
    while(1){
        L++;
        msleep(100);
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

void* thread_counter(void* VoidPtr){
    t_t *args = VoidPtr;
    int last_L = 0;
    while(1){
        int current_L = *args->L;
        if(current_L > last_L){
            if((*args->L % args->M) == 0){
                printf("Mnoznik: %d dzieli %d\n", args->M, *args->L);
        }
        last_L = current_L;
    }
    }
    return NULL;
}

void msleep(UINT milisec)
{
    time_t sec = (int)(milisec / 1000);
    milisec = milisec - (sec * 1000);
    timespec_t req = {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if (nanosleep(&req, &req))
        ERR("nanosleep");
}