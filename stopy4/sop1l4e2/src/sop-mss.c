#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ERR(source)                                                            \
  (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__),             \
   exit(EXIT_FAILURE))
#define UNUSED(x) ((void)(x))

#define DECK_SIZE (4 * 13)
#define HAND_SIZE (4)

typedef struct thread_args {
  pthread_t tid;
  int id;
  int n_players;
  unsigned int seed;
  int hand[HAND_SIZE];
  int *card_to_take;
  pthread_mutex_t *mtx_card_to_take;
  int *ready;
  pthread_mutex_t *mtx_start;
  pthread_cond_t *cond_start;
  int *winner;
  pthread_mutex_t *mtx_winner;
  pthread_barrier_t *barrier;
} thread_args_t;

void usage(const char *program_name) {
  fprintf(stderr, "USAGE: %s n\n", program_name);
  exit(EXIT_FAILURE);
}

void msleep(unsigned int msec) {
  time_t sec = (int)(msec / 1000);
  msec = msec - (sec * 1000);
  struct timespec req = {0};
  req.tv_sec = sec;
  req.tv_nsec = msec * 1000000L;
  if (TEMP_FAILURE_RETRY(nanosleep(&req, &req)))
    ERR("nanosleep");
}

void shuffle(int *array, size_t n) {
  if (n > 1) {
    size_t i;
    for (i = 0; i < n - 1; i++) {
      size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
      int t = array[j];
      array[j] = array[i];
      array[i] = t;
    }
  }
}

void print_deck(const int *deck, int size) {
  const char *suits[] = {" of Hearts", " of Diamonds", " of Clubs",
                         " of Spades"};
  const char *values[] = {"2", "3",  "4",    "5",     "6",    "7",  "8",
                          "9", "10", "Jack", "Queen", "King", "Ace"};

  char buffer[1024];
  int offset = 0;

  if (size < 1 || size > DECK_SIZE)
    return;

  offset += snprintf(buffer + offset, sizeof(buffer) - offset, "[");
  for (int i = 0; i < size; ++i) {
    int card = deck[i];
    if (card < 0 || card > DECK_SIZE)
      return;
    int suit = deck[i] % 4;
    int value = deck[i] / 4;

    offset +=
        snprintf(buffer + offset, sizeof(buffer) - offset, "%s", values[value]);
    offset +=
        snprintf(buffer + offset, sizeof(buffer) - offset, "%s", suits[suit]);
    if (i < size - 1)
      offset += snprintf(buffer + offset, sizeof(buffer) - offset, ", ");
  }
  snprintf(buffer + offset, sizeof(buffer) - offset, "]");

  puts(buffer);
}

void *run_player(void *_args) {
  thread_args_t *args = _args;
  pthread_t tid = args->tid;
  int id = args->id;
  int n_players = args->n_players;
  unsigned int seed = args->seed;
  int *hand = args->hand;
  int *card_to_take = args->card_to_take;
  pthread_mutex_t *mtx_card_to_take = args->mtx_card_to_take;
  int *ready = args->ready;
  pthread_mutex_t *mtx_start = args->mtx_start;
  pthread_cond_t *cond_start = args->cond_start;
  int *winner = args->winner;
  pthread_mutex_t *mtx_winner = args->mtx_winner;
  pthread_barrier_t *barrier = args->barrier;

  pthread_mutex_lock(mtx_start);
  while (*ready == 0) {
    if (pthread_cond_wait(cond_start, mtx_start))
      ERR("pthread_cond_wait");
  }
  pthread_mutex_unlock(mtx_start);
  printf("thread %ld with hand:\n", tid);
  print_deck(hand, HAND_SIZE);

  for (;;) {
    int ok = 1;
    for (int i = 1; i < HAND_SIZE; i++) {
      if (hand[i] % 4 != hand[0] % 4) {
        ok = 0;
        break;
      }
    }
    if (ok) {
      pthread_mutex_lock(mtx_winner);
      *winner = id;
      pthread_mutex_unlock(mtx_winner);
    }

    // pass a random card to your neighbor
    pthread_mutex_lock(mtx_card_to_take);
    int to_give = rand_r(&seed) % HAND_SIZE;
    card_to_take[(id + 1) % n_players] = hand[to_give];
    pthread_mutex_unlock(mtx_card_to_take);

    int res = pthread_barrier_wait(barrier);
    if (res == PTHREAD_BARRIER_SERIAL_THREAD) {
      printf("turn finished, current hand of thread %d:\n", id);
      print_deck(hand, HAND_SIZE);
    }
    pthread_mutex_lock(mtx_winner);
    if (*winner != 0) {
      if (*winner == id) {
        printf("%d wins with hand:\n", id);
        print_deck(hand, HAND_SIZE);
      }
      pthread_mutex_unlock(mtx_winner);
      printf("thread %d exited\n", id);
      return NULL;
    }
    pthread_mutex_unlock(mtx_winner);

    pthread_mutex_lock(mtx_card_to_take);
    hand[to_give] = card_to_take[id];
    pthread_mutex_unlock(mtx_card_to_take);
    // msleep(50);
  }

  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc != 2)
    usage(argv[0]);
  int n = atoi(argv[1]);
  if (n < 4 || n > 7)
    usage(argv[0]);

  srand(time(NULL));
  int deck[DECK_SIZE];
  for (int i = 0; i < DECK_SIZE; i++)
    deck[i] = i;
  // print_deck(deck, DECK_SIZE);

  thread_args_t *thread_args = malloc(n * sizeof(thread_args_t));
  if (thread_args == NULL)
    ERR("malloc");
  int *card_to_take = malloc(n * sizeof(int));
  if (card_to_take == NULL)
    ERR("malloc");
  memset(card_to_take, 0, n * sizeof(int));
  // int *stop = malloc(sizeof(int));
  // if (stop == NULL)
  //   ERR("malloc");
  pthread_mutex_t mtx_start = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t cond_start = PTHREAD_COND_INITIALIZER;
  pthread_mutex_t mtx_card_to_take = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t mtx_winner = PTHREAD_MUTEX_INITIALIZER;
  pthread_barrier_t barrier;
  pthread_barrier_init(&barrier, NULL, n);

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  // sigaddset(&mask, SIGINT);
  if (pthread_sigmask(SIG_BLOCK, &mask, NULL))
    ERR("pthread_sigmask");
  for (;;) {
    shuffle(deck, DECK_SIZE);
    int deck_ptr = 0, ready = 0, winner = 0, signo;
    for (int i = 0; i < n; i++) {
      printf("waiting for another player\n");
      if (sigwait(&mask, &signo))
        ERR("sigwait");
      if (signo == SIGINT) {
        thread_args[i].id = i;
        thread_args[i].n_players = n;
        thread_args[i].seed = rand();
        for (int j = 0; j < HAND_SIZE; j++)
          thread_args[i].hand[j] = deck[deck_ptr++];
        thread_args[i].card_to_take = card_to_take;
        thread_args[i].mtx_card_to_take = &mtx_card_to_take;
        thread_args[i].ready = &ready;
        thread_args[i].mtx_start = &mtx_start;
        thread_args[i].cond_start = &cond_start;
        thread_args[i].winner = &winner;
        thread_args[i].mtx_winner = &mtx_winner;
        thread_args[i].barrier = &barrier;
        if (pthread_create(&(thread_args[i].tid), NULL, run_player,
                           &thread_args[i]))
          ERR("pthread_create");
      }
    }
    pthread_mutex_lock(&mtx_start);
    ready = 1;
    pthread_cond_broadcast(&cond_start); // what if there's a thread that
                                         // isn't yet waiting on this condvar?
    pthread_mutex_unlock(&mtx_start);
    for (int i = 0; i < n; i++) {
      if (pthread_join(thread_args[i].tid, NULL))
        ERR("pthread_join");
    }
  }

  pthread_cond_destroy(&cond_start);
  pthread_mutex_destroy(&mtx_start);
  pthread_barrier_destroy(&barrier);
  // free(stop);
  free(thread_args);
  exit(EXIT_SUCCESS);
}