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

typedef struct timespec timespec_t;
typedef unsigned int UINT;
typedef struct dogs{
    pthread_t tid;
    UINT seed;
    int *array;
    int *torl;
    int position;
    int numer;
    int *kolejnosc;
    pthread_mutex_t *mxarray;
    int *first;
    int *second;
    int *third;
    int *ileskonczylo;
} dogs_t;

void msleep(UINT milisec);

int main(int argc, char** argv){
    int thread_count, tor_length;
    int kolej = 0;
    int skonczylo = 0;
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
    int first = 0;
    int second = 0;
    int third = 0;
    srand(time(NULL));
    for(int i =0;i<thread_count;i++){
        dogs[i].seed = rand();
        dogs[i].array = tor;
        dogs[i].mxarray = mxtor;
        dogs[i].torl = &tor_length;
        dogs[i].position = -1;
        dogs[i].numer = i+1;
        dogs[i].kolejnosc = &kolej;
        dogs[i].first = &first;
        dogs[i].second = &second;
        dogs[i].third = &third;
        dogs[i].ileskonczylo = &skonczylo;
    }
    for(int i = 0;i<thread_count;i++){
        if(pthread_create(&(dogs[i].tid), NULL, Dogf, &dogs[i])){
            ERR("pthread_create");
        }
    }

    while(1){
        if(skonczylo >= thread_count){
            break;
        }
        printf("[");
        for(int i = 0;i<tor_length;i++){
            printf("%d,", tor[i]);
        }
        printf("]");
        msleep(1000);
    }

    for(int i = 0;i<thread_count;i++){
        if(pthread_join(dogs[i].tid, NULL)){
            ERR("pthread_join");
        }
    }

    printf("first dog: %d, second dog: %d, thrid dog: %d\n", first, second, third);
    
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
    args->position = 0;
    pthread_mutex_lock(&args->mxarray[0]);
    args->array[0]++;
    pthread_mutex_unlock(&args->mxarray[0]);
    while(1){
        int los = rand_r(&args->seed) % 1320 + 200;
        msleep(los);
        int los2 = rand_r(&args->seed) % 5 + 1;
        if(args->position + los2 >= *args->torl-1){
            if(args->array[*args->torl-1] == 0){
                args->array[args->position]--; 
                args->position = *args->torl-1;
                printf("Dog number: %d, new position: %d\n", args->numer, args->position);
                printf("Dog number %d ended\n", args->numer);
                (*args->kolejnosc)++;
                printf("Dog number %d finished %d\n", args->numer, *args->kolejnosc);
                (*args->ileskonczylo)++;
                if(*args->first == 0){
                    *args->first = args->numer;
                }
                else if(*args->second == 0){
                    *args->second = args->numer;
                }
                else if(*args->third == 0){
                    *args->third = args->numer;
                }
                break;
            }
            else{
                printf("waf waf waf\n");
            }
        }
        else{
            if(args->array[args->position + los2] == 0){
                args->array[args->position]--;
                args->position = args->position + los2;
                args->array[args->position]++; 
                printf("Dog number: %d, new position: %d\n", args->numer, args->position);
            }
            else{
                printf("waf waf waf\n");
            }
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