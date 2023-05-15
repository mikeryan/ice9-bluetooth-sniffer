/*
 * Copyright 2023 ICE9 Consulting LLC
 */

#include <stdlib.h>

#include <fftw3.h>

#include "fft.h"

static fft_t fft_C[2];

extern int running;
extern unsigned channels;

static void _init_fft(fft_t *f, unsigned channels, unsigned batch_size) {
    int ch = channels;
    f->in  = malloc(sizeof(float complex) * channels * batch_size);
    f->out = malloc(sizeof(float complex) * channels * batch_size);
    f->plan = fftwf_plan_many_dft(1, &ch, batch_size,
                                  (fftwf_complex *)f->in,  NULL, 1, channels,
                                  (fftwf_complex *)f->out, NULL, 1, channels,
                                  FFTW_BACKWARD, FFTW_ESTIMATE);
    f->buffer_state = BUFFER_STATE_READY;
    pthread_cond_init(&f->buf_cond, NULL);
    pthread_mutex_init(&f->mutex, NULL);
}

void init_fft(unsigned channels, unsigned batch_size) {
    unsigned i;
    for (i = 0; i < 2; ++i)
        _init_fft(&fft_C[i], channels, batch_size);
}

void agc_submit(float complex *);
void *fft_thread_main(void *arg) {
    unsigned i;

    while (running) {
        for (i = 0; i < 2; ++i) {
            fft_t *f = &fft_C[i];
            pthread_mutex_lock(&f->mutex);
            while (running && f->buffer_state != BUFFER_STATE_EXECUTING)
                pthread_cond_wait(&f->buf_cond, &f->mutex);
            pthread_mutex_unlock(&f->mutex);
            if (!running)
                pthread_exit(NULL);

            fftwf_execute(f->plan);
            agc_submit(f->out);

            pthread_mutex_lock(&f->mutex);
            f->buffer_state = BUFFER_STATE_READY;
            pthread_cond_signal(&f->buf_cond);
            pthread_mutex_unlock(&f->mutex);
        }
    }
    return NULL;
}

static void _submit_fft(fft_t *f) {
    pthread_mutex_lock(&f->mutex);
    f->buffer_state = BUFFER_STATE_EXECUTING;
    pthread_cond_signal(&f->buf_cond);
    pthread_mutex_unlock(&f->mutex);
}

float complex *get_next_buffer(void) {
    static unsigned cur_fft = 1;
    fft_t *f = &fft_C[cur_fft];
    if (f->buffer_state == BUFFER_STATE_FILLING)
        _submit_fft(f);

    cur_fft = 1 - cur_fft;
    f = &fft_C[cur_fft];

    pthread_mutex_lock(&f->mutex);
    while (running && f->buffer_state != BUFFER_STATE_READY)
        pthread_cond_wait(&f->buf_cond, &f->mutex);
    if (!running) {
        pthread_mutex_unlock(&f->mutex);
        pthread_exit(NULL);
    }
    f->buffer_state = BUFFER_STATE_FILLING;
    pthread_mutex_unlock(&f->mutex);

    return f->in;
}
