/*
 * Copyright (c) 2022 ICE9 Consulting LLC
 */

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <libbladeRF.h>

#include "sdr.h"

const int bladerf_gain_val = 30;
const unsigned num_transfers = 7;

extern sig_atomic_t running;
extern pid_t self_pid;
extern unsigned channels;
extern float samp_rate;
extern unsigned center_freq;

unsigned timeouts = 0;

int *bladerf_list(unsigned *num_out) {
    struct bladerf_devinfo *devices;
    int i, num;
    int *ret = NULL;

    num = bladerf_get_device_list(&devices);
    if (num == 0 || num == BLADERF_ERR_NODEV) {
        num = 0;
        goto out;
    }
    if (num < 0)
        errx(1, "Unable to get bladeRF device list: %s", bladerf_strerror(num));

    ret = malloc(sizeof(*ret) * num);
    for (i = 0; i < num; ++i)
        ret[i] = devices[i].instance;

out:
    if (num != 0)
        bladerf_free_device_list(devices);
    *num_out = num;
    return ret;
}

struct bladerf *bladerf_setup(int id) {
    int status;
    char identifier[32];
    struct bladerf *bladerf = NULL;
    snprintf(identifier, sizeof(identifier), "*:instance=%d", id);

    bladerf_set_usb_reset_on_open(true);
    if ((status = bladerf_open(&bladerf, identifier)) != 0)
        errx(1, "Unable to open bladeRF: %s", bladerf_strerror(status));
    if ((status = bladerf_set_sample_rate(bladerf, BLADERF_CHANNEL_RX(0), samp_rate, NULL)) != 0)
        errx(1, "Unable to set bladeRF sample rate: %s", bladerf_strerror(status));
    // TODO bandwidth
    if ((status = bladerf_set_frequency(bladerf, BLADERF_CHANNEL_RX(0), center_freq * 1e6)) != 0)
        errx(1, "Unable to set bladeRF center frequency: %s", bladerf_strerror(status));
    if ((status = bladerf_set_gain_mode(bladerf, BLADERF_CHANNEL_RX(0), BLADERF_GAIN_MGC)) != 0)
        errx(1, "Unable to set bladeRF manual gain control: %s", bladerf_strerror(status));
    if ((status = bladerf_set_gain(bladerf, BLADERF_CHANNEL_RX(0), bladerf_gain_val)) != 0)
        errx(1, "Unable to get bladeRF gain: %s", bladerf_strerror(status));

    if (strcmp("bladerf1", bladerf_get_board_name(bladerf)) == 0) {
        // TODO
        errx(1, "bladeRF 1 not supported yet");
    }

    return bladerf;
}

void *bladerf_rx_cb(struct bladerf *bladerf, struct bladerf_stream *stream, struct bladerf_metadata *meta, void *samples, size_t num_samples, void *user_data) {
    unsigned i;
    int16_t *d = (int16_t *)samples;

    timeouts = 0;

    sample_buf_t *s = malloc(sizeof(*s) + num_samples * sizeof(float complex));
    s->num = num_samples;
    for (i = 0; i < num_samples; ++i)
        s->samples[i] = d[2*i] / 2048.0f + d[2*i+1] / 2048.0f * I;

    if (running)
        push_samples(s);
    else
        free(s);

    return samples;
}

void *bladerf_stream_thread(void *arg) {
    struct bladerf *bladerf = (struct bladerf *)arg;
    struct bladerf_stream *stream;
    void **buffers = NULL;
    unsigned timeout;
    int status;

    if ((status = bladerf_init_stream(&stream, bladerf, bladerf_rx_cb, &buffers, num_transfers, BLADERF_FORMAT_SC16_Q11, channels / 2 * 4096, num_transfers, NULL)) != 0)
        errx(1, "Unable to initialize bladeRF stream: %s", bladerf_strerror(status));

    // FIXME get the 4096 out of here
    timeout = 1000 * channels / 2 * 4096 / samp_rate;
    if (bladerf_set_stream_timeout(bladerf, BLADERF_MODULE_RX, timeout * (num_transfers + 2)) != 0)
        errx(1, "Unable to set bladeRF timeout");

    if (bladerf_enable_module(bladerf, BLADERF_MODULE_RX, true) != 0)
        errx(1, "Unable to enable bladeRF RX module");

    timeouts = 0;
    while (running) {
        if ((status = bladerf_stream(stream, BLADERF_MODULE_RX)) < 0) {
            if (status != BLADERF_ERR_TIMEOUT)
                break;
            if (++timeouts < 5)
                continue;
            warnx("bladeRF timed out too many times, giving up");
            running = 0;
        }
    }

    running = 0;
    bladerf_enable_module(bladerf, BLADERF_MODULE_RX, false);

    kill(self_pid, SIGINT);

    return NULL;
}
