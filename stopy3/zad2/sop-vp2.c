#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include "video-player.h" // Zakładam, że tu jest BUFFER_SIZE (np. 16)

#define FPS_INTERVAL 33333333L

// --- Zmienna globalna do flagi zakończenia (volatile) ---
// Dostępna dla wątków, by wiedziały kiedy wyjść z pętli
volatile sig_atomic_t is_running = 1;

// --- Funkcje pomocnicze czasu ---
void msleep(long ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

void nsleep(long ns) {
    struct timespec ts = { .tv_sec = 0, .tv_nsec = ns };
    nanosleep(&ts, NULL);
}

// --- Bufor Cykliczny ---
typedef struct circular_buffer {
    video_frame *data[BUFFER_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
} circular_buffer;

void cb_init(circular_buffer *cb) {
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
    if (pthread_mutex_init(&cb->mutex, NULL) != 0) ERR("mutex init");
}

void cb_destroy(circular_buffer *cb) {
    pthread_mutex_destroy(&cb->mutex);
}

// Push z obsługą wyjścia (zwraca 0 gdy sukces, -1 gdy koniec programu)
int cb_push(circular_buffer *cb, video_frame *frame) {
    while (is_running) { // Sprawdzamy flagę
        pthread_mutex_lock(&cb->mutex);
        if (cb->count < BUFFER_SIZE) {
            cb->data[cb->head] = frame;
            cb->head = (cb->head + 1) % BUFFER_SIZE;
            cb->count++;
            pthread_mutex_unlock(&cb->mutex);
            return 0;
        }
        pthread_mutex_unlock(&cb->mutex);
        msleep(5); // Busy wait
    }
    return -1; // Program się kończy
}

// Pop z obsługą wyjścia
video_frame* cb_pop(circular_buffer *cb) {
    video_frame *frame = NULL;
    while (is_running) { // Sprawdzamy flagę
        pthread_mutex_lock(&cb->mutex);
        if (cb->count > 0) {
            frame = cb->data[cb->tail];
            cb->tail = (cb->tail + 1) % BUFFER_SIZE;
            cb->count--;
            pthread_mutex_unlock(&cb->mutex);
            return frame;
        }
        pthread_mutex_unlock(&cb->mutex);
        msleep(5); // Busy wait
    }
    return NULL; // Program się kończy
}

// Struktura argumentów dla wątków
typedef struct {
    circular_buffer *buf_dec_to_trans;
    circular_buffer *buf_trans_to_disp;
} shared_data_t;

// --- WĄTEK 1: DEKODUJĄCY ---
void* thread_decoder(void* arg) {
    shared_data_t *args = (shared_data_t*)arg;
    while (is_running) {
        video_frame *new_frame = decode_frame();
        if (cb_push(args->buf_dec_to_trans, new_frame) != 0) {
            // Jeśli program się kończy, musimy zwolnić klatkę, której nie udało się wypchnąć
            free(new_frame); 
            break;
        }
    }
    return NULL;
}

// --- WĄTEK 2: TRANSFORMUJĄCY ---
void* thread_transformer(void* arg) {
    shared_data_t *args = (shared_data_t*)arg;
    video_frame *frame = NULL;
    while (is_running) {
        frame = cb_pop(args->buf_dec_to_trans);
        if (frame == NULL) break; // Sygnał stopu

        transform_frame(frame);
        
        if (cb_push(args->buf_trans_to_disp, frame) != 0) {
            free(frame); // Sprzątanie przy wyjściu
            break;
        }
    }
    return NULL;
}

// --- WĄTEK 3: WYŚWIETLAJĄCY ---
void* thread_display(void* arg) {
    shared_data_t *args = (shared_data_t*)arg;
    video_frame *frame = NULL;
    struct timespec last_time, current_time;

    if (clock_gettime(CLOCK_REALTIME, &last_time)) ERR("clock");

    while (is_running) {
        frame = cb_pop(args->buf_trans_to_disp);
        if (frame == NULL) break; // Sygnał stopu

        // Synchronizacja FPS
        if (clock_gettime(CLOCK_REALTIME, &current_time)) ERR("clock");
        long elapsed_ns = ELAPSED(last_time, current_time);
        
        if (elapsed_ns < FPS_INTERVAL) {
            nsleep(FPS_INTERVAL - elapsed_ns);
        }
        if (clock_gettime(CLOCK_REALTIME, &last_time)) ERR("clock");

        display_frame(frame); // Ta funkcja zwalnia pamięć (free)
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    // 1. Blokowanie sygnałów w wątku głównym (zanim stworzymy inne wątki)
    // Dzięki temu nowo utworzone wątki odziedziczą maskę i nie będą przerywane przez SIGINT.
    sigset_t newMask, oldMask;
    sigemptyset(&newMask);
    sigaddset(&newMask, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &newMask, &oldMask) != 0) ERR("SIG_BLOCK error");

    // 2. Inicjalizacja buforów (na stosie)
    circular_buffer buf1, buf2;
    cb_init(&buf1);
    cb_init(&buf2);

    shared_data_t data;
    data.buf_dec_to_trans = &buf1;
    data.buf_trans_to_disp = &buf2;

    pthread_t th1, th2, th3;
    if (pthread_create(&th1, NULL, thread_decoder, &data)) ERR("create");
    if (pthread_create(&th2, NULL, thread_transformer, &data)) ERR("create");
    if (pthread_create(&th3, NULL, thread_display, &data)) ERR("create");

    // 3. Oczekiwanie na sygnał SIGINT (zamiast sleep i pętli)
    // To jest eleganckie rozwiązanie z Zadania 3.
    int signo;
    if (sigwait(&newMask, &signo) != 0) ERR("sigwait");
    
    // Otrzymano SIGINT -> Ustawiamy flagę
    printf("\nOtrzymano SIGINT. Kończenie pracy...\n");
    is_running = 0;

    // 4. Czekanie na wątki
    // Wątki same wyjdą z pętli while(is_running) przy następnym sprawdzeniu
    if (pthread_join(th1, NULL)) ERR("join");
    if (pthread_join(th2, NULL)) ERR("join");
    if (pthread_join(th3, NULL)) ERR("join");

    // 5. Sprzątanie
    cb_destroy(&buf1);
    cb_destroy(&buf2);
    
    // Przywrócenie maski sygnałów (opcjonalne przy wyjściu)
    if (pthread_sigmask(SIG_UNBLOCK, &newMask, &oldMask)) ERR("SIG_UNBLOCK error");

    return EXIT_SUCCESS;
}