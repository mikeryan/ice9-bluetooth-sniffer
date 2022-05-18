/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <libhackrf/hackrf.h>

#include "sdr.h"

const unsigned vga_gain = 32;
const unsigned lna_gain = 32;

extern float samp_rate;
extern unsigned center_freq;
extern char *serial;
extern sig_atomic_t running;

hackrf_device *hackrf_setup(void) {
    hackrf_device *hackrf;

    hackrf_init();

    if (serial == NULL) {
        if (hackrf_open(&hackrf) != HACKRF_SUCCESS)
            errx(1, "Unable to open HackRF");
    } else {
        if (hackrf_open_by_serial(serial, &hackrf) != HACKRF_SUCCESS)
            errx(1, "Unable to open HackRF");
    }
    if (hackrf_set_sample_rate(hackrf, samp_rate) != HACKRF_SUCCESS)
        errx(1, "Unable to set HackRF sample rate");
    if (hackrf_set_freq(hackrf, center_freq * 1e6) != HACKRF_SUCCESS)
        errx(1, "Unable to set HackRF center frequency");
    if (hackrf_set_vga_gain(hackrf, vga_gain) != HACKRF_SUCCESS)
        errx(1, "Unable to set HackRF VGA gain");
    if (hackrf_set_lna_gain(hackrf, lna_gain) != HACKRF_SUCCESS)
        errx(1, "Unable to set HackRF LNA gain");

    return hackrf;
}

int hackrf_rx_cb(hackrf_transfer *t) {
    unsigned i;
    sample_buf_t *s = malloc(sizeof(*s) + t->valid_length * 4);
    s->num = t->valid_length / 2;
    for (i = 0; i < s->num; ++i)
        s->samples[i] = ((int8_t *)t->buffer)[2*i] / 128.0f + ((int8_t *)t->buffer)[2*i+1] / 128.0f * I;
    if (running)
        push_samples(s);
    else
        free(s);
    return 0;
}
