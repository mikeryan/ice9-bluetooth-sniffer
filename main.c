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

#include <btbb.h>
#include <liquid/liquid.h>

#include "bladerf.h"
#include "bluetooth.h"
#include "burst_catcher.h"
#include "fsk.h"
#include "hackrf.h"
#include "sdr.h"

#define C_FEK_BLOCKING_QUEUE_IMPLEMENTATION
#define C_FEK_FAIR_LOCK_IMPLEMENTATION
#include "blocking_queue.h"

// only needed on macOS
#include "pthread_barrier.h"

float samp_rate = 0.f;
unsigned channels = 20;
unsigned center_freq = 2427;
lell_pcap_handle *pcap = NULL;
char *base_name = NULL;
int live = 0;
FILE *in = NULL;
char *serial = NULL;
int bladerf_num = -1;
int verbose = 0;
int stats = 0;

sig_atomic_t running = 1;
pid_t self_pid;

unsigned sps(void) { return (unsigned)(samp_rate / channels / 1e6f * 2.0f); }

const float sym_rate = 1e6f;
const float lp_cutoff = 0.75f; // cutoff in MHz
const unsigned m = 4; // magical polyphase filter bank number (filter half-length)

#define AGC_BUFFER_SIZE 4096
typedef struct _agc_buffer_t {
    float complex buffer[AGC_BUFFER_SIZE];
} agc_buffer_t;
agc_buffer_t *agc_live = NULL, *agc_dead = NULL;
unsigned agc_live_size = 0, agc_dead_size = 0;
agc_buffer_t *agc_buffers;
unsigned live_buf = 0;

pthread_t *agc_threads;
pthread_mutex_t agc_buf_mutex;
pthread_cond_t agc_buf_ready, agc_buf_done;
pthread_barrier_local_t agc_barrier;
unsigned long agc_start, agc_end;

static burst_catcher_t *catcher = NULL;
static firpfbch2_crcf magic;

sample_buf_t *next_samples = NULL;
pthread_t channelizer;
pthread_mutex_t samples_mutex;
pthread_cond_t samples_ready, samples_done;

#define BURST_QUEUE_SIZE 64
Blocking_Queue bursts;
pthread_t burst_processor;

pthread_t spewer;

// requires (channels / 2) samples
void pfbch_execute_block(float complex *samples) {
    unsigned i;
    float complex out[channels];

    firpfbch2_crcf_execute(magic, samples, out);
    for (i = 0; i < channels; ++i)
        agc_live[i].buffer[agc_live_size] = out[i];
    ++agc_live_size;
}

void push_samples(sample_buf_t *buf) {
    pthread_mutex_lock(&samples_mutex);
    while (running && next_samples != NULL)
        pthread_cond_wait(&samples_done, &samples_mutex);
    if (!running) {
        pthread_mutex_unlock(&samples_mutex);
        free(buf);
        return;
    }
    next_samples = buf;
    pthread_cond_signal(&samples_ready);
    pthread_mutex_unlock(&samples_mutex);
}

static inline unsigned long now_us(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long)now.tv_sec * 1000000lu + (unsigned long)now.tv_nsec / 1000lu;
}

unsigned long ch_sum = 0;

void trigger_agc(void) {
    const unsigned avg_count = 100;
    static unsigned long sum = 0;
    static unsigned sum_count = 0;

    pthread_mutex_lock(&agc_buf_mutex);
    while (running && agc_dead != NULL)
        pthread_cond_wait(&agc_buf_done, &agc_buf_mutex);
    if (!running) {
        pthread_mutex_unlock(&agc_buf_mutex);
        pthread_exit(NULL);
    }
    agc_dead = agc_live;
    agc_dead_size = agc_live_size;
    live_buf = 1 - live_buf;
    agc_live = &agc_buffers[channels * live_buf];
    agc_live_size = 0;
    pthread_cond_broadcast(&agc_buf_ready);
    pthread_mutex_unlock(&agc_buf_mutex);

    if (stats) {
        sum += agc_end - agc_start;
        if (++sum_count == avg_count) {
            double eff_samp_rate = (channels-2) * AGC_BUFFER_SIZE * avg_count * 1e6 / sum;
            double rel_rate = eff_samp_rate / ((channels-2) * 2e6);
            double ch_samp_rate = AGC_BUFFER_SIZE * channels / 2 * avg_count * 1e6 / ch_sum;
            double ch_rel_rate = ch_samp_rate / samp_rate;
            printf("agc %'11.0f samp/sec (%5.0f%% realtime); ch %'11.0f samp/sec (%5.0f%% realtime)\n", eff_samp_rate, 100.0 * rel_rate, ch_samp_rate, 100 * ch_rel_rate);
            if (rel_rate < 1.0)
                printf("AGC is too slow, use fewer channels\n");
            if (ch_rel_rate < 1.0)
                printf("Channelizer too slow, use fewer channels\n");
            sum_count = sum = ch_sum = 0;
        }
        agc_start = now_us();
    }
}

void *channelizer_thread(void *arg) {
    unsigned i;
    sample_buf_t *samples = NULL;
    static unsigned long ch_start = 0;

    while (running) {
        // get next samples
        pthread_mutex_lock(&samples_mutex);
        while (running && next_samples == NULL)
            pthread_cond_wait(&samples_ready, &samples_mutex);
        if (!running) {
            pthread_mutex_unlock(&samples_mutex);
            return NULL;
        }
        samples = next_samples;
        next_samples = NULL;
        pthread_mutex_unlock(&samples_mutex);

        // channelize them
        for (i = 0; running && i + channels / 2 <= samples->num; i += channels / 2) {
            pfbch_execute_block(&samples->samples[i]);
            if (agc_live_size == AGC_BUFFER_SIZE) {
                if (stats)
                    ch_sum += now_us() - ch_start;
                trigger_agc();
                if (stats)
                    ch_start = now_us();
            }
        }

        free(samples);
        if (!running)
            return NULL;

        pthread_mutex_lock(&samples_mutex);
        pthread_cond_signal(&samples_done);
        pthread_mutex_unlock(&samples_mutex);
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
            if (burst_catcher_execute(&catcher[id], &agc_dead[id].buffer[i], burst)) {
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

void *spewer_thread(void *in_ptr) {
    size_t r;
    FILE *in = (FILE *)in_ptr;

    sample_buf_t *samples = malloc(sizeof(*samples) + sizeof(float complex) * channels * AGC_BUFFER_SIZE);
    while (running && (r = fread(&samples->samples, sizeof(float complex) * channels * AGC_BUFFER_SIZE, 1, in)) > 0) {
        samples->num = channels * AGC_BUFFER_SIZE;
        push_samples(samples);
        samples = malloc(sizeof(*samples) + sizeof(float complex) * channels * AGC_BUFFER_SIZE);
    }
    free(samples);
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
            bluetooth_detect(burst->packet.bits, burst->packet.bits_len, burst->freq, &lap, &aa);

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
    pthread_mutex_init(&agc_buf_mutex, NULL);
    pthread_mutex_init(&samples_mutex, NULL);
    pthread_cond_init(&agc_buf_ready, NULL);
    pthread_cond_init(&agc_buf_done, NULL);
    pthread_cond_init(&samples_ready, NULL);
    pthread_cond_init(&samples_done, NULL);
    pthread_barrier_local_init(&agc_barrier, NULL, channels-2);
    blocking_queue_init(&bursts, BURST_QUEUE_SIZE);
    agc_threads = calloc(channels, sizeof(*agc_threads));
    pthread_create(&channelizer, NULL, channelizer_thread, NULL);
#ifdef __linux__
    pthread_setname_np(channelizer, "channlizer");
#endif
    for (i = 1; i < channels; ++i) {
        if (i != channels/2) {
            pthread_create(&agc_threads[i], NULL, agc_thread, (void *)i);
#ifdef __linux__
            char name[32];
            snprintf(name, sizeof(name), "agc-%02lu", i);
            pthread_setname_np(agc_threads[i], name);
#endif
        }
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

void kick_rx_cb(void) {
    pthread_mutex_lock(&samples_mutex);
    pthread_cond_signal(&samples_done);
    pthread_mutex_unlock(&samples_mutex);
}

void deinit_threads(int join_spewer) {
    uintptr_t i;
    running = 0;

    if (join_spewer)
        pthread_join(spewer, NULL);

    pthread_mutex_lock(&samples_mutex);
    pthread_cond_signal(&samples_ready);
    pthread_mutex_unlock(&samples_mutex);
    pthread_join(channelizer, NULL);

    pthread_mutex_lock(&agc_buf_mutex);
    pthread_cond_broadcast(&agc_buf_ready);
    pthread_cond_signal(&agc_buf_done);
    pthread_mutex_unlock(&agc_buf_mutex);
    pthread_barrier_local_shutdown(&agc_barrier);
    for (i = 1; i < channels; ++i)
        if (i != channels/2)
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
    pthread_t bladerf_thread;

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
        else
            hackrf = hackrf_setup();
    }
    btbb_init(1);

    unsigned h_len = 2*channels*m + 1;
    float *h = malloc(sizeof(float) * h_len);
    liquid_firdes_kaiser(h_len, lp_cutoff/(float)channels, 60.0f, 0.0f, h);
    magic = firpfbch2_crcf_create(LIQUID_ANALYZER, channels, m, h);
    free(h);

    agc_buffers = malloc(2 * channels * sizeof(*agc_buffers));
    agc_live = &agc_buffers[channels * live_buf];

    catcher = malloc(sizeof(burst_catcher_t) * channels);
    for (i = 1; i < channels; ++i)
        burst_catcher_create(&catcher[i], center_freq + (i < channels / 2 ? i : -channels + i));

    init_threads(!live);

    if (live) {
        if (hackrf != NULL)
            hackrf_start_rx(hackrf, hackrf_rx_cb, NULL);
        else
            pthread_create(&bladerf_thread, NULL, bladerf_stream_thread, (void *)bladerf);
    }

    while (running) {
        if (live && hackrf != NULL && !hackrf_is_streaming(hackrf))
            break;
        pause();
    }
    running = 0;

    kick_rx_cb();

    if (live) {
        if (hackrf != NULL)
            hackrf_stop_rx(hackrf);
        else
            bladerf_enable_module(bladerf, BLADERF_MODULE_RX, false);
    }

    deinit_threads(!live);

    if (live) {
        if (hackrf != NULL) {
            hackrf_close(hackrf);
            hackrf_exit();
        } else {
            pthread_join(bladerf_thread, NULL);
            bladerf_close(bladerf);
        }
    }

    if (pcap)
        lell_pcap_close(pcap);

    for (i = 1; i < channels; ++i)
        burst_catcher_destroy(&catcher[i]);
    free(catcher);
    firpfbch2_crcf_destroy(magic);

    return 0;
}
