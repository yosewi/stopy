#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "video-player.h"

#define FPS_INTERVAL 33333333L

void msleep(long ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

void nsleep(long ns) {
    struct timespec ts = { .tv_sec = 0, .tv_nsec = ns };
    nanosleep(&ts, NULL);
}

typedef struct {
    video_frame *frame_dec_to_trans;
    pthread_mutex_t *mx_dec_to_trans;

    video_frame *frame_trans_to_disp;
    pthread_mutex_t *mx_trans_to_disp;
} shared_data_t;

void* thread_decoder(void* arg) {
    shared_data_t *args = (shared_data_t*)arg;

    while (1) {
        video_frame *new_frame = decode_frame();

        while (1) {
            pthread_mutex_lock(args->mx_dec_to_trans);
            if (args->frame_dec_to_trans == NULL) {
                args->frame_dec_to_trans = new_frame;
                pthread_mutex_unlock(args->mx_dec_to_trans);
                break; 
            } else {
                pthread_mutex_unlock(args->mx_dec_to_trans);
                msleep(5);
            }
        }
    }
    return NULL;
}

void* thread_transformer(void* arg) {
    shared_data_t *args = (shared_data_t*)arg;
    video_frame *current_frame = NULL;

    while (1) {
        while (1) {
            pthread_mutex_lock(args->mx_dec_to_trans);
            if (args->frame_dec_to_trans != NULL) {
                current_frame = args->frame_dec_to_trans;
                args->frame_dec_to_trans = NULL;
                pthread_mutex_unlock(args->mx_dec_to_trans);
                break;
            } else {
                pthread_mutex_unlock(args->mx_dec_to_trans);
                msleep(5);
            }
        }

        transform_frame(current_frame);

        while (1) {
            pthread_mutex_lock(args->mx_trans_to_disp);
            if (args->frame_trans_to_disp == NULL) {
                args->frame_trans_to_disp = current_frame;
                pthread_mutex_unlock(args->mx_trans_to_disp);
                break;
            } else {
                pthread_mutex_unlock(args->mx_trans_to_disp);
                msleep(5);
            }
        }
    }
    return NULL;
}

void* thread_display(void* arg) {
    shared_data_t *args = (shared_data_t*)arg;
    video_frame *frame_to_show = NULL;

    struct timespec last_time, current_time;

    if(clock_gettime(CLOCK_REALTIME, &last_time)){
        ERR("clock_gettime");
    }

    while (1) {
        while (1) {
            pthread_mutex_lock(args->mx_trans_to_disp);
            if (args->frame_trans_to_disp != NULL) {
                frame_to_show = args->frame_trans_to_disp;
                args->frame_trans_to_disp = NULL;
                pthread_mutex_unlock(args->mx_trans_to_disp);
                break;
            } else {
                pthread_mutex_unlock(args->mx_trans_to_disp);
                msleep(5);
            }
        }

        if(clock_gettime(CLOCK_REALTIME, &current_time)){
            ERR("clock_gettime");
        }

        long elapsed_ns = ELAPSED(last_time, current_time);

        if(elapsed_ns < FPS_INTERVAL){
            nsleep(FPS_INTERVAL - elapsed_ns);
        }

        if(clock_gettime(CLOCK_REALTIME, &last_time)){
            ERR("clock_gettime");
        }

        display_frame(frame_to_show);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    shared_data_t data;
    
    data.frame_dec_to_trans = NULL;
    data.frame_trans_to_disp = NULL;

    pthread_mutex_t mx1 = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mx2 = PTHREAD_MUTEX_INITIALIZER;

    data.mx_dec_to_trans = &mx1;
    data.mx_trans_to_disp = &mx2;

    pthread_t th1, th2, th3;

    if (pthread_create(&th1, NULL, thread_decoder, &data) != 0) ERR("create 1");
    if (pthread_create(&th2, NULL, thread_transformer, &data) != 0) ERR("create 2");
    if (pthread_create(&th3, NULL, thread_display, &data) != 0) ERR("create 3");

    if (pthread_join(th1, NULL) != 0) ERR("join 1");
    if (pthread_join(th2, NULL) != 0) ERR("join 2");
    if (pthread_join(th3, NULL) != 0) ERR("join 3");

    return EXIT_SUCCESS;
}