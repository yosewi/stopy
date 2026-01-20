#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))
#define UNUSED(x) ((void)(x))

#define DECK_SIZE (4 * 13)
#define HAND_SIZE (7)

typedef struct thread_args{
    pthread_t tid;
    int id;
    int n_players;
    unsigned int seed;
    int hand[HAND_SIZE];
} thread_args_t;

void usage(const char *program_name)
{
    fprintf(stderr, "USAGE: %s n\n", program_name);
    exit(EXIT_FAILURE);
}

void shuffle(int *array, size_t n)
{
    if (n > 1)
    {
        size_t i;
        for (i = 0; i < n - 1; i++)
        {
            size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
            int t = array[j];
            array[j] = array[i];
            array[i] = t;
        }
    }
}

void print_deck(const int *deck, int size)
{
    const char *suits[] = {" of Hearts", " of Diamonds", " of Clubs", " of Spades"};
    const char *values[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "Jack", "Queen", "King", "Ace"};

    char buffer[1024];
    int offset = 0;

    if (size < 1 || size > DECK_SIZE)
        return;

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "[");
    for (int i = 0; i < size; ++i)
    {
        int card = deck[i];
        if (card < 0 || card > DECK_SIZE)
            return;
        int suit = deck[i] % 4;
        int value = deck[i] / 4;

        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s", values[value]);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s", suits[suit]);
        if (i < size - 1)
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, ", ");
    }
    snprintf(buffer + offset, sizeof(buffer) - offset, "]");

    puts(buffer);
}

void* threadfunc(void* args){
    thread_args_t* t_args = (thread_args_t*)args;
    printf("Watek %d (TID: %lu) posiada karty:\n", t_args->id,(unsigned long)t_args->tid);
    print_deck(t_args->hand, HAND_SIZE);
    return NULL;
}

int main(int argc, char *argv[])
{
    if(argc != 2) return 1;
    int n = atoi(argv[1]);

    srand(time(NULL));

    int deck[DECK_SIZE];
    for(int i = 0;i<DECK_SIZE;i++){
        deck[i] = i;
    }
    shuffle(deck, DECK_SIZE);

    thread_args_t* players = (thread_args_t*)malloc(sizeof(thread_args_t) * n);
    int deck_idx = 0;

    for(int i = 0;i<n;i++){
        players[i].id = i;
        players[i].n_players = n;
        players[i].seed = rand();
        for(int k = 0;k<HAND_SIZE;k++){
            players[i].hand[k] = deck[deck_idx++];
        }

        if(pthread_create(&players[i].tid, NULL, threadfunc, &players[i])){
            ERR("pthread_create");
        }
    }

    for(int i = 0;i<n;i++){
        pthread_join(players[i].tid, NULL);
    }

    free(players);

    exit(EXIT_SUCCESS);
}
