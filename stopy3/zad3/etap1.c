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
#include <signal.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void ReadArguments(int argc, char **argv, int *torLength, int *threadCount);
void* Dogf(void *voidArgs);

typedef unsigned int UINT;
typedef struct dogs{
    pthread_t tid;
    UINT seed;
    int *array;
    int *torl;
    pthread_mutex_t *mxarray;
} dogs_t;

int main(int argc, char** argv){
    int thread_count, tor_length;
    ReadArguments(argc, argv, &tor_length, &thread_count);
    int *tor = (int*)malloc(sizeof(int) * tor_length);
    if(tor == NULL){
        ERR("malloc");
    }
    pthread_mutex_t *mxtor = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t) * tor_length);
    if(mxtor == NULL){
        ERR("malloc");
    }
    dogs_t *dogs = (dogs_t*)malloc(sizeof(dogs_t) * thread_count);
    if(dogs == NULL){
        ERR("malloc");
    }
    for(int i = 0;i<tor_length;i++){
        tor[i] = 0;
        if(pthread_mutex_init(&mxtor[i], NULL)){
            ERR("pthread_mutex_init");
        }
    }
    srand(time(NULL));
    for(int i =0;i<thread_count;i++){
        dogs[i].seed = rand();
        dogs[i].array = tor;
        dogs[i].mxarray = mxtor;
        dogs[i].torl = &tor_length;
    }
    for(int i = 0;i<thread_count;i++){
        if(pthread_create(&(dogs[i].tid), NULL, Dogf, &dogs[i])){
            ERR("pthread_create");
        }
    }

    for(int i = 0;i<thread_count;i++){
        if(pthread_join(dogs[i].tid, NULL)){
            ERR("pthread_join");
        }
    }

    printf("[");
    for(int i = 0;i<tor_length;i++){
        printf("%d,", tor[i]);
    }
    printf("]");
    
    for(int i = 0;i<tor_length;i++){
        pthread_mutex_destroy(&mxtor[i]);
    }
    free(tor);
    free(mxtor);
    free(dogs);
    exit(EXIT_SUCCESS);
}

void ReadArguments(int argc, char **argv, int *torLength, int *threadCount){
    if(argc >= 2){
        *torLength = atoi(argv[1]);
        if(*torLength < 20){
            printf("tor length gotta be > 20\n");
            exit(EXIT_FAILURE);
        }
        *threadCount = atoi(argv[2]);
        if(*threadCount < 3){
            printf("gotta put more than 2 dogs\n");
            exit(EXIT_FAILURE);
        }
    }
}

void* Dogf(void *voidArgs){
    dogs_t* args = voidArgs;
    int index = rand_r(&args->seed) % *args->torl;

    pthread_mutex_lock(&args->mxarray[index]);
    args->array[index]++;
    pthread_mutex_unlock(&args->mxarray[index]);
    return NULL;
}
