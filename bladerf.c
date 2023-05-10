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
int num_samples_workaround = 0;

#if defined(LIBBLADERF_API_VERSION) && (LIBBLADERF_API_VERSION >= 0x02050000)
#define BLADERF_OVERSAMPLE
#endif

void bladerf_list(void) {
    struct bladerf_devinfo *devices;
    int i, num;

    num = bladerf_get_device_list(&devices);
    if (num == 0 || num == BLADERF_ERR_NODEV) {
        num = 0;
        goto out;
    }
    if (num < 0)
        errx(1, "Unable to get bladeRF device list: %s", bladerf_strerror(num));

    for (i = 0; i < num; ++i)
        printf("interface {value=bladerf%i}{display=ICE9 Bluetooth}\n", devices[i].instance);

out:
    if (num != 0)
        bladerf_free_device_list(devices);
}

struct bladerf *bladerf_setup(int id) {
    struct bladerf_version version;
    int status;
    char identifier[32];
    struct bladerf *bladerf = NULL;
    snprintf(identifier, sizeof(identifier), "*:instance=%d", id);

    bladerf_version(&version);
    if (version.major == 2 && version.minor == 5 && version.patch == 0)
        num_samples_workaround = 1;

    bladerf_set_usb_reset_on_open(true);
    if ((status = bladerf_open(&bladerf, identifier)) != 0)
        errx(1, "Unable to open bladeRF: %s", bladerf_strerror(status));

#ifdef BLADERF_OVERSAMPLE
    if ((status = bladerf_enable_feature(bladerf, BLADERF_FEATURE_OVERSAMPLE, true)) != 0)
        errx(1, "Unable to set bladeRF to oversample mode: %s", bladerf_strerror(status));
#endif

    // TODO adjust bw based on capture rate
    if ((status = bladerf_set_bandwidth(bladerf, BLADERF_CHANNEL_RX(0), 56000000, NULL)) != 0)
        errx(1, "Unable to set bladeRF bandwidth: %s", bladerf_strerror(status));
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
#ifdef BLADERF_OVERSAMPLE
    int8_t *d = (int8_t *)samples;
#else
    int16_t *d = (int16_t *)samples;
#endif

    timeouts = 0;
    if (num_samples_workaround) // see https://github.com/Nuand/bladeRF/pull/916
        num_samples *= 2;

    sample_buf_t *s = malloc(sizeof(*s) + num_samples * sizeof(int8_t) * 2);
    s->num = num_samples;
    for (i = 0; i < num_samples * 2; ++i)
#ifdef BLADERF_OVERSAMPLE
        s->samples[i] = d[i];
#else
        s->samples[i] = d[2*i] / 2048.0f + d[2*i+1] / 2048.0f * I;
#endif

    if (running)
        push_samples(s);
    else
        free(s);

    return samples;
}

void *bladerf_stream_thread(void *arg) {
    struct bladerf *bladerf = (struct bladerf *)arg;
    struct bladerf_stream *stream;
    struct bladerf_rational_rate rate = { .integer = samp_rate, .num = 0, .den = 1 };
    void **buffers = NULL;
    unsigned timeout;
    int status;

#ifdef BLADERF_OVERSAMPLE
    if ((status = bladerf_init_stream(&stream, bladerf, bladerf_rx_cb, &buffers, num_transfers, BLADERF_FORMAT_SC8_Q7, channels / 2 * 4096, num_transfers, NULL)) != 0)
#else
    if ((status = bladerf_init_stream(&stream, bladerf, bladerf_rx_cb, &buffers, num_transfers, BLADERF_FORMAT_SC16_Q11, channels / 2 * 4096, num_transfers, NULL)) != 0)
#endif
        errx(1, "Unable to initialize bladeRF stream: %s", bladerf_strerror(status));

    // must occur after the change to 8 bit samples
    if ((status = bladerf_set_rational_sample_rate(bladerf, BLADERF_CHANNEL_RX(0), &rate, NULL)) != 0)
        errx(1, "Unable to set bladeRF sample rate: %s", bladerf_strerror(status));

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
