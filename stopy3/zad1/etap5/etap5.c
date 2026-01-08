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

#define THREAD_COUNT 10

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

typedef struct timespec timespec_t;
typedef unsigned int UINT;
typedef struct t{
    pthread_t tid;
    UINT seed;
    int M;
    int* L;
    int* licznik;
    pthread_mutex_t *mxlicznik;
    sigset_t *pMask;
    bool *pQuitFlag;
    pthread_mutex_t *pmxQuitFlag;
} t_t;
void* thread_counter();
void msleep(UINT milisec);

void ReadArguments(int argc, char **argv, int *threadCount);
void *signal_handling(void *voidArgs);

int main(int argc, char **argv){
    int threadCount;
    int L = 1;
    bool quitFlag = 0;
    int licznik = 0;
    ReadArguments(argc, argv, &threadCount);
    pthread_mutex_t pmxlicznik = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mxQuitFlag = PTHREAD_MUTEX_INITIALIZER;
    t_t *watki = (t_t*)malloc(sizeof(t_t) * threadCount);
    if(watki == NULL){
        ERR("malloc");
    }
    sigset_t oldMask, newMask;
    sigemptyset(&newMask);
    sigaddset(&newMask, SIGINT);
    if(pthread_sigmask(SIG_BLOCK, &newMask, &oldMask)){
        ERR("pthread_sigmask");
    }
    pthread_t signal_tid;
    if(pthread_create(&signal_tid, NULL, signal_handling, &watki[0])){
        ERR("pthread_create_signal");
    }
    srand(time(NULL));
    for(int i = 0;i<threadCount;i++){
        watki[i].seed = rand();
        watki[i].L = &L;
        watki[i].M = rand_r(&watki[i].seed) % 99 + 2;
        watki[i].licznik = &licznik;
        watki[i].mxlicznik = &pmxlicznik;
        watki[i].pMask = &newMask;
        watki[i].pQuitFlag = &quitFlag;
        watki[i].pmxQuitFlag = &mxQuitFlag;
    }
    for(int i =0;i<threadCount;i++){
        if(pthread_create(&(watki[i].tid), NULL, thread_counter, &watki[i])){
            ERR("pthread_create");
        }
    }
    while(1){
        pthread_mutex_lock(&mxQuitFlag);
        if(quitFlag == true){
            pthread_mutex_unlock(&mxQuitFlag);
            break;
        }
        else{
            pthread_mutex_unlock(&mxQuitFlag);
            pthread_mutex_lock(&pmxlicznik);
            if(licznik >= threadCount){
                licznik = 0;
                L++;
            }
            pthread_mutex_unlock(&pmxlicznik);
            msleep(100);
        }
    }
    for(int i = 0;i<threadCount;i++){
        if(pthread_join(watki[i].tid, NULL)){
            ERR("pthread_join");
        }
    }
    free(watki);
    if(pthread_sigmask(SIG_UNBLOCK, &newMask, &oldMask)){
        ERR("SIG_UNBLOCK");
    }
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
    while((*args->pQuitFlag) == 0){
        int current_L = *args->L;
        if(current_L > last_L){
            if((*args->L % args->M) == 0){
                printf("Mnoznik: %d dzieli %d\n", args->M, *args->L);
            }
        pthread_mutex_lock(args->mxlicznik);
        (*args->licznik)++;
        pthread_mutex_unlock(args->mxlicznik);
        }
        last_L = current_L;
        msleep(1);
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

void *signal_handling(void *voidArgs)
{
    t_t *args = voidArgs;
    int signo;
    srand(time(NULL));
    for(;;){
        if(sigwait(args->pMask, &signo)){
            ERR("sigwait failed");
        }
        switch(signo){
            case SIGINT:
                pthread_mutex_lock(args->pmxQuitFlag);
                (*args->pQuitFlag) = true;
                pthread_mutex_unlock(args->pmxQuitFlag);
                return NULL;
            default:
                exit(1);
        }
    }
    return NULL;
}