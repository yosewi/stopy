#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CIRC_BUF_SIZE 1024
#define DEFAULT_N_THREADS 3
#define ALPHABET_SIZE 128

#define ERR(source)                                                            \
  (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__),             \
   kill(0, SIGKILL), exit(EXIT_FAILURE))
#define NEXT_DOUBLE(seedptr) ((double)rand_r(seedptr) / (double)RAND_MAX)

typedef struct circular_buffer {
  char *buf[CIRC_BUF_SIZE];
  int head;
  int tail;
  int len;
  pthread_mutex_t mx;
} circ_buf;

typedef struct thread_args {
  pthread_t tid;
  unsigned int seed;
  circ_buf *cb;
  int *freq;
  pthread_mutex_t *mx_freq;
  sigset_t *mask;
  int *quit;
  pthread_mutex_t *mx_quit;
} thread_args_t;

void msleep(unsigned msec) {
  time_t sec = (int)(msec / 1000);
  msec = msec - (sec * 1000);
  struct timespec req = {0};
  req.tv_sec = sec;
  req.tv_nsec = msec * 1000000L;
  if (TEMP_FAILURE_RETRY(nanosleep(&req, &req)))
    ERR("nanosleep");
}

circ_buf *circ_buf_create() {
  circ_buf *cb = malloc(sizeof(circ_buf));
  if (cb == NULL)
    ERR("malloc");
  for (int i = 0; i < CIRC_BUF_SIZE; i++) {
    cb->buf[i] = NULL;
  }
  cb->head = 0;
  cb->tail = 0;
  cb->len = 0;
  if (pthread_mutex_init(&cb->mx, NULL)) {
    free(cb);
    ERR("pthread_mutex_init");
  }

  return cb;
}

void circ_buf_destroy(circ_buf *cb) {
  if (cb == NULL)
    return;
  pthread_mutex_destroy(&cb->mx);
  for (int i = 0; i < CIRC_BUF_SIZE; i++) {
    free(cb->buf[i]);
  }
  free(cb);
}

void circ_buf_push(circ_buf *cb, char *path) {
  if (cb == NULL)
    return;
  while (cb->len == CIRC_BUF_SIZE) {
    msleep(5);
  }
  char *s = malloc((strlen(path) + 1) * sizeof(char));
  if (s == NULL)
    ERR("malloc");
  strcpy(s, path);
  pthread_mutex_lock(&cb->mx);
  free(cb->buf[cb->head]);
  cb->buf[cb->head] = s;
  cb->len++;
  cb->head++;
  if (cb->head == CIRC_BUF_SIZE)
    cb->head = 0;
  pthread_mutex_unlock(&cb->mx);
}

char *circ_buf_pop(circ_buf *cb) {
  if (cb == NULL)
    ERR("circ_buf is NULL");
  for (;;) {
    pthread_mutex_lock(&cb->mx);
    if (cb->len > 0) {
      char *s = cb->buf[cb->tail];
      cb->len--;
      cb->tail++;
      if (cb->tail == CIRC_BUF_SIZE)
        cb->tail = 0;
      pthread_mutex_unlock(&cb->mx);
      return s;
    }
    pthread_mutex_unlock(&cb->mx);
    msleep(5);
  }
}

void read_args(int argc, char *argv[], int *n_threads) {
  *n_threads = DEFAULT_N_THREADS;
  if (argc >= 2) {
    *n_threads = atoi(argv[1]);
    if (*n_threads <= 0) {
      printf("n_threads must be positive\n");
      exit(EXIT_FAILURE);
    }
  }
}

int has_ext(char *path, char *ext) {
  char *ext_pos = strrchr(path, '.');
  return ext_pos && strcmp(ext_pos, ext) == 0;
}

void walk_dir(char *path, circ_buf *cb) {
  DIR *dir = opendir(path);
  if (dir == NULL)
    ERR("opendir");
  struct dirent *entry;
  struct stat statbuf;
  while ((entry = readdir(dir)) != NULL) {
    char *entry_name = entry->d_name;
    if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0)
      continue;
    int path_len = strlen(path);
    int new_path_len = path_len + strlen(entry_name) + 2;
    char *new_path = malloc(new_path_len * sizeof(char));
    if (new_path == NULL)
      ERR("malloc");
    strcpy(new_path, path);
    new_path[path_len] = '/';
    strcpy(new_path + path_len + 1, entry_name);

    if (stat(new_path, &statbuf) < 0)
      ERR("stat");
    if (S_ISDIR(statbuf.st_mode)) {
      walk_dir(new_path, cb);
    } else if (S_ISREG(statbuf.st_mode)) {
      if (has_ext(entry_name, ".txt"))
        circ_buf_push(cb, new_path);
    }
    free(new_path);
  }
  if (closedir(dir))
    ERR("closedir");
}

ssize_t bulk_read(int fd, char *buf, size_t count) {
  ssize_t c;
  ssize_t len = 0;
  do {
    c = TEMP_FAILURE_RETRY(read(fd, buf, count));
    if (c < 0)
      return c;
    if (c == 0)
      return len; // EOF
    buf += c;
    len += c;
    count -= c;
  } while (count > 0);
  return len;
}

void *thread_work(void *_args) {
  thread_args_t *args = _args;
  sigset_t *mask = args->mask;
  if (pthread_sigmask(SIG_BLOCK, mask, NULL))
    ERR("pthread_sigmask");
  circ_buf *cb = args->cb;
  unsigned int seed = args->seed;
  int *freq = args->freq;
  pthread_mutex_t *mx_freq = args->mx_freq;
  int *quit = args->quit;
  pthread_mutex_t *mx_quit = args->mx_quit;

  char *c = malloc(sizeof(char));
  if (c == NULL)
    ERR("malloc");
  while (cb->len > 0) {
    char *path = circ_buf_pop(cb);
    printf("thread %u, processing %s\n", seed, path);
    int fd = TEMP_FAILURE_RETRY(open(path, O_RDONLY));
    if (fd < 0)
      ERR("open");
    while (bulk_read(fd, c, 1) > 0) {
      msleep(2); // so that the files aren't read "instanly"
      pthread_mutex_lock(mx_quit);
      if (*quit == 1) {
        pthread_mutex_unlock(mx_quit);
        free(c);
        if (TEMP_FAILURE_RETRY(close(fd)) < 0)
          ERR("close");
        printf("thread %u ending\n", seed);
        return NULL;
      }
      pthread_mutex_unlock(mx_quit);

      // printf("thread %u, read %c\n", seed, *c);
      pthread_mutex_lock(mx_freq);
      freq[(int)*c]++;
      pthread_mutex_unlock(mx_freq);
    }
    if (TEMP_FAILURE_RETRY(close(fd)) < 0)
      ERR("close");
  }
  free(c);
  printf("thread %u ending\n", seed);
  return NULL;
}

void *signal_handling(void *_args) {
  thread_args_t *args = _args;
  int *freq = args->freq;
  pthread_mutex_t *mx_freq = args->mx_freq;
  sigset_t *mask = args->mask;
  int *quit = args->quit;
  pthread_mutex_t *mx_quit = args->mx_quit;

  int signo;
  for (;;) {
    if (sigwait(mask, &signo))
      ERR("sigwait");
    if (signo == SIGUSR1) {
      printf("freqs: ");
      pthread_mutex_lock(mx_freq);
      for (char c = 'a'; c <= 'z'; c++) {
        int f = freq[(int)c];
        if (f > 0)
          printf("%c: %d, ", c, f);
      }
      pthread_mutex_unlock(mx_freq);
      printf("\n");
    } else if (signo == SIGINT) {
      pthread_mutex_lock(mx_quit);
      *quit = 1;
      pthread_mutex_unlock(mx_quit);
      return NULL;
    }
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  int n_threads;
  read_args(argc, argv, &n_threads);
  // +1 for the additional signal-handling thread
  thread_args_t *thread_args = malloc((n_threads + 1) * sizeof(thread_args_t));
  if (thread_args == NULL)
    ERR("malloc");

  circ_buf *cb = circ_buf_create();
  walk_dir("data1", cb);
  srand(time(NULL));
  int *freq = malloc(ALPHABET_SIZE * sizeof(int));
  if (freq == NULL)
    ERR("malloc");
  memset(freq, 0, ALPHABET_SIZE * sizeof(int));
  pthread_mutex_t mx_freq = PTHREAD_MUTEX_INITIALIZER;
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  sigaddset(&mask, SIGINT);
  if (pthread_sigmask(SIG_BLOCK, &mask, NULL))
    ERR("pthread_sigmask");
  int quit = 0;
  pthread_mutex_t mx_quit = PTHREAD_MUTEX_INITIALIZER;
  for (int i = 0; i < n_threads + 1; i++) {
    thread_args[i].cb = cb;
    thread_args[i].seed = rand();
    thread_args[i].freq = freq;
    thread_args[i].mx_freq = &mx_freq;
    thread_args[i].mask = &mask;
    thread_args[i].quit = &quit;
    thread_args[i].mx_quit = &mx_quit;
  }
  if (pthread_create(&(thread_args[n_threads].tid), NULL, signal_handling,
                     &thread_args[n_threads]))
    ERR("pthread_create");
  for (int i = 0; i < n_threads; i++) {
    if (pthread_create(&(thread_args[i].tid), NULL, thread_work,
                       &thread_args[i]))
      ERR("pthread_create");
  }
  for (;;) {
    msleep(100);
    pthread_mutex_lock(&mx_quit);
    if (quit == 1) {
      pthread_mutex_unlock(&mx_quit);
      break;
    }
    pthread_mutex_unlock(&mx_quit);
    kill(0, SIGUSR1);
  }
  for (int i = 0; i < n_threads + 1; i++) {
    if (pthread_join(thread_args[i].tid, NULL))
      ERR("pthread_join");
  }
  free(freq);
  free(thread_args);
  circ_buf_destroy(cb);

  exit(EXIT_SUCCESS);
}