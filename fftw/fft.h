/*
 * Copyright 2023 ICE9 Consulting LLC
 */

#pragma once

#include <complex.h>
#include <pthread.h>

#include <fftw3.h>

typedef struct _fft_t {
    fftwf_plan plan;
    float complex *in;
    float complex *out;
    enum buffer_state_t {
        BUFFER_STATE_READY,
        BUFFER_STATE_FILLING,
        BUFFER_STATE_EXECUTING,
        BUFFER_STATE_DONE,
        BUFFER_STATE_EMPTYING,
    } buffer_state;
    pthread_cond_t buf_cond;
    pthread_mutex_t mutex;
} fft_t;

void init_fft(unsigned channels, unsigned batch_size);
float complex *get_next_buffer(void);
void *fft_thread_main(void *);
