/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#define _GNU_SOURCE
#include <complex.h>
#include <err.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <liquid/liquid.h>

#include "bladerf.h"
#include "bluetooth.h"
#include "btbb/btbb.h"
#include "burst_catcher.h"
#include "fft.h"
#include "fsk.h"
#include "hackrf.h"
#include "pcap.h"
#include "sdr.h"
#include "usrp.h"

#include "pfbch2.h"

#define C_FEK_BLOCKING_QUEUE_IMPLEMENTATION
#define C_FEK_FAIR_LOCK_IMPLEMENTATION
#include "blocking_queue.h"

// only needed on macOS
#include "pthread_barrier.h"

float samp_rate = 0.f;
unsigned channels = 96;
unsigned live_ch[40];
unsigned center_freq = 2441;
pcap_t *pcap = NULL;
char *base_name = NULL;
int live = 0;
FILE *in = NULL;
char *serial = NULL;
char *usrp_serial = NULL;
int bladerf_num = -1;
int verbose = 0;
int stats = 0;

volatile sig_atomic_t running = 1;
pid_t self_pid;

unsigned sps(void) { return (unsigned)(samp_rate / channels / 1e6f * 2.0f); }

const float lp_cutoff = 0.75f; // cutoff in MHz
const unsigned m = 4; // magical polyphase filter bank number (filter half-length)

#define AGC_BUFFER_SIZE 4096
#define BATCH_SIZE 4096
typedef struct _agc_buffer_t {
    float complex buffer[AGC_BUFFER_SIZE];
} agc_buffer_t;
agc_buffer_t *agc_live = NULL, *agc_dead = NULL;
unsigned agc_live_size = 0, agc_dead_size = 0;
agc_buffer_t *agc_buffers;
unsigned live_buf = 0;

#ifdef USE_FFTW
pthread_t fft_thread;
#else
pthread_t agc_dispatcher;
#endif
pthread_mutex_t agc_dispatch_mutex;
pthread_cond_t fft_done_cond;
pthread_cond_t dispatch_done_cond;
pthread_t *agc_threads;
pthread_mutex_t agc_buf_mutex;
pthread_cond_t agc_buf_ready, agc_buf_done;
pthread_barrier_local_t agc_barrier;
unsigned long agc_start, agc_end;

static burst_catcher_t *catcher = NULL;
static pfbch2_t magic;

#define SAMPLES_QUEUE_SIZE 16384
Blocking_Queue samples_queue;
pthread_t channelizer;

#define BURST_QUEUE_SIZE 64
Blocking_Queue bursts;
pthread_t burst_processor;

pthread_t spewer;

// special case for all channels
void pfbch_execute_block_96(int8_t *samples) {
    static float complex *buf = NULL;
    static unsigned buf_pos = 0;

    unsigned i;
    int16_t out[96*2]; // FIXME (max number of channels we support)

    if (buf == NULL)
        buf = get_next_buffer();

    pfbch2_execute(&magic, samples, out);
    for (i = 0; i < 96; ++i)
        buf[96 * buf_pos + i] = out[2*i] / 32768.f + out[2*i + 1] / 32768.f * I;

    if (++buf_pos == BATCH_SIZE) {
        buf = get_next_buffer();
        buf_pos = 0;
    }
}

void pfbch_execute_block(int8_t *samples) {
    static float complex *buf = NULL;
    static unsigned buf_pos = 0;

    unsigned i;
    int16_t out[96*2]; // FIXME (max number of channels we support)

    if (buf == NULL)
        buf = get_next_buffer();

    pfbch2_execute(&magic, samples, out);
    for (i = 0; i < channels; ++i)
        buf[channels * buf_pos + i] = out[2*i] / 32768.f + out[2*i + 1] / 32768.f * I;

    if (++buf_pos == BATCH_SIZE) {
        buf = get_next_buffer();
        buf_pos = 0;
    }
}

void push_samples(sample_buf_t *buf) {
    if (blocking_queue_add(&samples_queue, buf) == BQ_FULL) {
        if (verbose)
            printf("WARNING: dropped samples on the floor. try fewer channels or a bigger buffer.\n");
        free(buf);
    }
}

static inline unsigned long now_us(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long)now.tv_sec * 1000000lu + (unsigned long)now.tv_nsec / 1000lu;
}

unsigned long ch_sum = 0;

static void *fft;
static float complex *fft_out;
static unsigned long ch_start = 0;

void fft_done(void *f, void *out) {
    pthread_mutex_lock(&agc_dispatch_mutex);
    while (running && fft_out != NULL)
        pthread_cond_wait(&dispatch_done_cond, &agc_dispatch_mutex);
    if (!running) {
        pthread_mutex_unlock(&agc_dispatch_mutex);
        pthread_exit(NULL);
    }
    fft = f;
    fft_out = out;
    pthread_cond_signal(&fft_done_cond);
    pthread_mutex_unlock(&agc_dispatch_mutex);
}

void agc_submit(float complex *fft_out) {
    const unsigned avg_count = 100;
    static unsigned long sum = 0;
    static unsigned sum_count = 0;
    unsigned i, j;

    for (i = 0; i < channels; ++i)
        for (j = 0; j < BATCH_SIZE; ++j)
            agc_live[i].buffer[j] = fft_out[j * channels + i] / (float)channels;

    if (stats) {
        unsigned long now = now_us();
        ch_sum += now - ch_start;
        ch_start = now;
    }

    pthread_mutex_lock(&agc_buf_mutex);
    while (running && agc_dead != NULL)
        pthread_cond_wait(&agc_buf_done, &agc_buf_mutex);
    if (!running) {
        pthread_mutex_unlock(&agc_buf_mutex);
        pthread_exit(NULL);
    }

    agc_dead = agc_live;
    agc_dead_size = BATCH_SIZE; //agc_live_size;
    live_buf = 1 - live_buf;
    agc_live = &agc_buffers[channels * live_buf];
    agc_live_size = 0;
    pthread_cond_broadcast(&agc_buf_ready);
    pthread_mutex_unlock(&agc_buf_mutex);

    if (stats) {
        sum += agc_end - agc_start;
        if (++sum_count == avg_count) {
            double eff_samp_rate = 40 * AGC_BUFFER_SIZE * avg_count * 2e6 / sum;
            double rel_rate = eff_samp_rate / ((channels-2) * 2e6);
            double ch_samp_rate = AGC_BUFFER_SIZE * channels / 2 * avg_count * 1e6 / ch_sum;
            double ch_rel_rate = ch_samp_rate / samp_rate;
            printf("agc %'11.0f samp/sec (%5.0f%% realtime); ch %'11.0f samp/sec (%5.0f%% realtime)\n", eff_samp_rate, 100.0 * rel_rate, ch_samp_rate, 100 * ch_rel_rate);
            if (rel_rate < 0.99)
                printf("AGC is too slow, use fewer channels\n");
            if (ch_rel_rate < 0.99)
                printf("Channelizer too slow, use fewer channels\n");
            sum_count = sum = ch_sum = 0;
        }
        agc_start = now_us();
    }
}

#ifndef USE_FFTW
void *agc_dispatcher_thread(void *arg) {
    static float complex my_fft[96 * BATCH_SIZE];

    while (running) {
        pthread_mutex_lock(&agc_dispatch_mutex);
        while (running && fft_out == NULL)
            pthread_cond_wait(&fft_done_cond, &agc_dispatch_mutex);
        if (!running) {
            pthread_mutex_unlock(&agc_dispatch_mutex);
            pthread_exit(NULL);
        }
        memcpy(my_fft, fft_out, BATCH_SIZE * channels * sizeof(float complex));
        release_buffer(fft);
        fft_out = NULL;
        pthread_cond_signal(&dispatch_done_cond);
        pthread_mutex_unlock(&agc_dispatch_mutex);

        agc_submit(my_fft);
    }
    return NULL;
}
#endif

void *channelizer_thread(void *arg) {
    unsigned i;
    sample_buf_t *samples = NULL;

    while (running) {
        // get next samples
        if (blocking_queue_take(&samples_queue, &samples) != 0)
            return NULL;

        if (channels == 96) {
            for (i = 0; running && i + channels / 2 <= samples->num; i += channels / 2)
                pfbch_execute_block(&samples->samples[2*i]);
        } else {
            // channelize them
            for (i = 0; running && i + channels / 2 <= samples->num; i += channels / 2)
                pfbch_execute_block_96(&samples->samples[2*i]);
        }

        free(samples);
    }
    return NULL;
}

void *agc_thread(void *id_ptr) {
    unsigned id = (uintptr_t)id_ptr;
    unsigned i;
    burst_t *burst = calloc(1, sizeof(*burst));

    while (running) {
        pthread_mutex_lock(&agc_buf_mutex);
        while (running && agc_dead == NULL)
            pthread_cond_wait(&agc_buf_ready, &agc_buf_mutex);
        pthread_mutex_unlock(&agc_buf_mutex);
        if (!running)
            goto out;

        for (i = 0; i < agc_dead_size; ++i) {
            if (burst_catcher_execute(&catcher[id], &agc_dead[live_ch[id]].buffer[i], burst)) {
                if (burst->len < 132) { // FIXME
                    burst_destroy(burst);
                    memset(burst, 0, sizeof(*burst));
                } else {
                    if (blocking_queue_add(&bursts, burst) == BQ_FULL && verbose)
                        printf("WARNING: dropped burst on the floor. try fewer channels.\n");
                    burst = calloc(1, sizeof(*burst));
                }
            }
        }

        if (!running)
            goto out;

        if (pthread_barrier_local_wait(&agc_barrier) == 1) {
            // we're the lucky thread!
            if (stats)
                agc_end = now_us();
            pthread_mutex_lock(&agc_buf_mutex);
            agc_dead = NULL;
            agc_dead_size = 0;
            pthread_cond_signal(&agc_buf_done);
            pthread_mutex_unlock(&agc_buf_mutex);
        }
        if (!running)
            goto out;
        pthread_barrier_local_wait(&agc_barrier);
    }
out:
    free(burst);
    return NULL;
}

int queue_empty(volatile Blocking_Queue *q) {
    return q->queue_size == 0;
}

void *spewer_thread(void *in_ptr) {
    size_t r;
    FILE *in = (FILE *)in_ptr;

    sample_buf_t *samples = malloc(sizeof(*samples) + sizeof(int8_t) * 2 * channels * AGC_BUFFER_SIZE);
    while (running && (r = fread(&samples->samples, sizeof(int8_t) * 2 * channels * AGC_BUFFER_SIZE, 1, in)) > 0) {
        samples->num = channels * AGC_BUFFER_SIZE;
        if (blocking_queue_put(&samples_queue, samples) != 0) {
            free(samples);
            return NULL;
        }
        samples = malloc(sizeof(*samples) + sizeof(int8_t) * 2 * channels * AGC_BUFFER_SIZE);
    }
    free(samples);

    while (running && !queue_empty(&samples_queue))
        ;
    running = 0;
    kill(self_pid, SIGINT);

    return NULL;
}

void *burst_processor_thread(void *arg) {
    fsk_demod_t fsk;
    burst_t *burst;

    fsk_demod_init(&fsk);

    while (running) {
        if (blocking_queue_take(&bursts, &burst) != 0)
            goto out;

        fsk_demod(&fsk, burst->burst, burst->len, burst->freq, &burst->packet);

        if (burst->packet.demod != NULL && burst->packet.bits != NULL) {
            uint32_t lap = 0xffffffff, aa = 0xffffffff;
            bluetooth_detect(burst->packet.bits, burst->packet.bits_len, burst->freq, burst->timestamp, &lap, &aa);

            if (verbose) {
                printf("burst %4u-%04u ", burst->freq, burst->num);
                printf("cfo %f deviation %f ", burst->packet.cfo, burst->packet.deviation);
                if (lap != 0xffffffff)
                    printf("lap %06x", lap);
                if (aa != 0xffffffff)
                    printf("aa %08x", aa);
                printf("\n");
            }

            if (base_name != NULL) {
                char *filename;
                FILE *out;

                /* burst */
                (void)!asprintf(&filename, "%s-%04u-%04u.fc32", base_name, burst->freq, burst->num);
                out = fopen(filename, "w");
                if (out == NULL)
                    err(1, "Unable to create file %s", filename);
                free(filename);
                fwrite(burst->burst, sizeof(float complex), burst->len, out);
                fclose(out);

                /* demoded samples */
                (void)!asprintf(&filename, "%s-%04u-%04u.f32", base_name, burst->freq, burst->num);
                out = fopen(filename, "w");
                if (out == NULL)
                    err(1, "Unable to create file %s", filename);
                free(filename);
                fwrite(burst->packet.demod, sizeof(float), burst->len, out);
                fclose(out);

                /* cfo / maybe other metadata? */
                (void)!asprintf(&filename, "%s-%04u-%04u.txt", base_name, burst->freq, burst->num);
                out = fopen(filename, "w");
                if (out == NULL)
                    err(1, "Unable to create file %s", filename);
                free(filename);
                fprintf(out, "cfo=%f\n", burst->packet.cfo);
                fprintf(out, "silence=%u\n", burst->packet.silence);
                if (lap != 0xffffffff)
                    fprintf(out, "lap=%06x\n", lap);
                if (aa != 0xffffffff)
                    fprintf(out, "aa=%08x\n", aa);
                fclose(out);
            }
        }
        burst_destroy(burst);
        free(burst);
    }
out:
    fsk_demod_destroy(&fsk);
    return NULL;
}

void init_threads(int launch_spewer) {
    uintptr_t i;
    pthread_mutex_init(&agc_dispatch_mutex, NULL);
    pthread_mutex_init(&agc_buf_mutex, NULL);
    pthread_cond_init(&fft_done_cond, NULL);
    pthread_cond_init(&dispatch_done_cond, NULL);
    pthread_cond_init(&agc_buf_ready, NULL);
    pthread_cond_init(&agc_buf_done, NULL);
    pthread_barrier_local_init(&agc_barrier, NULL, 40);
    blocking_queue_init(&samples_queue, SAMPLES_QUEUE_SIZE);
    blocking_queue_init(&bursts, BURST_QUEUE_SIZE);
    agc_threads = calloc(channels, sizeof(*agc_threads));
    pthread_create(&channelizer, NULL, channelizer_thread, NULL);
#ifdef USE_FFTW
    pthread_create(&fft_thread, NULL, fft_thread_main, NULL);
#else
    pthread_create(&agc_dispatcher, NULL, agc_dispatcher_thread, NULL);
#endif
#ifdef __linux__
    pthread_setname_np(channelizer, "channelizer");
#ifdef USE_FFTW
    pthread_setname_np(fft_thread, "fft");
#else
    pthread_setname_np(agc_dispatcher, "agc-dispatcher");
#endif
#endif
    for (i = 0; i < 40; ++i) {
        pthread_create(&agc_threads[i], NULL, agc_thread, (void *)i);
#ifdef __linux__
        char name[32];
        snprintf(name, sizeof(name), "agc-%04lu", 2402+i*2);
        pthread_setname_np(agc_threads[i], name);
#endif
    }
    pthread_create(&burst_processor, NULL, burst_processor_thread, NULL);
#ifdef __linux__
    pthread_setname_np(burst_processor, "burst_processor");
#endif
    if (launch_spewer) {
        pthread_create(&spewer, NULL, spewer_thread, (void *)in);
#ifdef __linux__
        pthread_setname_np(spewer, "spewer");
#endif
    }
}

void deinit_threads(int join_spewer) {
    uintptr_t i;
    running = 0;

    if (join_spewer)
        pthread_join(spewer, NULL);

    blocking_queue_close(&samples_queue);
    pthread_join(channelizer, NULL);

    pthread_mutex_lock(&agc_buf_mutex);
    pthread_cond_broadcast(&agc_buf_ready);
    pthread_cond_signal(&agc_buf_done);
    pthread_mutex_unlock(&agc_buf_mutex);
    pthread_barrier_local_shutdown(&agc_barrier);
    for (i = 0; i < 40; ++i)
        pthread_join(agc_threads[i], NULL);

    blocking_queue_close(&bursts);
    pthread_join(burst_processor, NULL);
}

void sig(int signo) {
    running = 0;
}

void parse_options(int argc, char **argv);

int main(int argc, char **argv) {
    unsigned i;
    // char *out_filename = NULL;
    hackrf_device *hackrf = NULL;
    struct bladerf *bladerf = NULL;
    uhd_usrp_handle usrp = NULL;
    pthread_t bladerf_thread, usrp_thread;

    signal(SIGINT, sig);
    signal(SIGTERM, sig);
    signal(SIGPIPE, sig);
    self_pid = getpid();

    // enables , separator in printf
    setlocale(LC_NUMERIC, "");

    parse_options(argc, argv);

    if (live) {
        // TODO select first available interface
        if (bladerf_num >= 0)
            bladerf = bladerf_setup(bladerf_num);
        else if (usrp_serial != NULL)
            usrp = usrp_setup(usrp_serial);
        else
            hackrf = hackrf_setup();
    }
    gen_syndrome_map(1);

    unsigned h_len = 2*channels*m + 1;
    float *h = malloc(sizeof(float) * h_len);
    liquid_firdes_kaiser(h_len, lp_cutoff/(float)channels, 60.0f, 0.0f, h);
    pfbch2_init(&magic, channels, m, h);
    init_fft(channels, BATCH_SIZE);
    free(h);

    agc_buffers = malloc(2 * channels * sizeof(*agc_buffers));
    agc_live = &agc_buffers[channels * live_buf];

    catcher = malloc(sizeof(burst_catcher_t) * channels);
    for (i = 0; i < channels; ++i) {
        unsigned freq = center_freq + (i < channels / 2 ? i : -channels + i);
        if ((freq & 1) == 0 && freq >= 2402 && freq <= 2480)
            live_ch[(freq-2402)/2] = i;
    }
    for (i = 0; i < 40; ++i)
        burst_catcher_create(&catcher[i], 2402 + i * 2);

    init_threads(!live);

    if (live) {
        if (hackrf != NULL)
            hackrf_start_rx(hackrf, hackrf_rx_cb, NULL);
        else if (usrp != NULL)
            pthread_create(&usrp_thread, NULL, usrp_stream_thread, (void *)usrp);
        else
            pthread_create(&bladerf_thread, NULL, bladerf_stream_thread, (void *)bladerf);
    }

    while (running) {
        if (live && hackrf != NULL && !hackrf_is_streaming(hackrf))
            break;
        pause();
    }
    running = 0;

    if (live) {
        if (hackrf != NULL)
            hackrf_stop_rx(hackrf);
        else if (usrp != NULL)
            ; // do nothing (stream is stopped in thread)
        else
            bladerf_enable_module(bladerf, BLADERF_MODULE_RX, false);
    }

    deinit_threads(!live);

    if (live) {
        if (hackrf != NULL) {
            hackrf_close(hackrf);
            hackrf_exit();
        } else if (usrp != NULL) {
            pthread_join(usrp_thread, NULL);
            usrp_close(usrp);
        } else {
            pthread_join(bladerf_thread, NULL);
            bladerf_close(bladerf);
        }
    }

    if (pcap)
        pcap_close(pcap);

    for (i = 0; i < 40; ++i)
        burst_catcher_destroy(&catcher[i]);
    free(catcher);

    return 0;
}
