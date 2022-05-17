/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#include <complex.h>
#include <string.h>
#include <stdlib.h>

#include <liquid/liquid.h>

#include "burst_catcher.h"

const float sql = -35.0f; // agc squelch
const float bt = 0.25f; // agc bandwidth

// starting size of burst buffer in floats
#define BURST_START_SIZE 2048

void burst_catcher_create(burst_catcher_t *c, unsigned freq) {
    memset(c, 0, sizeof(*c));
    c->freq = freq;

    // agc
    c->agc = agc_crcf_create();
    agc_crcf_set_bandwidth(c->agc, bt);
    agc_crcf_set_signal_level(c->agc,1e-3f);     // initial guess at starting signal level

    agc_crcf_squelch_enable(c->agc);             // enable squelch
    agc_crcf_squelch_set_threshold(c->agc, sql); // threshold for detection [dB]
    agc_crcf_squelch_set_timeout  (c->agc, 100); // timeout for hysteresis
}

void burst_catcher_destroy(burst_catcher_t *c) {
    free(c->agc);
    free(c->burst);
}

int burst_catcher_execute(burst_catcher_t *c, float complex *sample, burst_t *burst_out) {
    agc_crcf_execute(c->agc, *sample, sample);
    if (agc_crcf_squelch_get_status(c->agc) == LIQUID_AGC_SQUELCH_SIGNALHI) {
        if (c->burst_len == c->burst_buf_size) {
            c->burst_buf_size *= 2;
            c->burst = realloc(c->burst, sizeof(float complex) * c->burst_buf_size);
        }
        c->burst[c->burst_len++] = *sample;
    } else if (agc_crcf_squelch_get_status(c->agc) == LIQUID_AGC_SQUELCH_RISE) {
        c->burst = malloc(sizeof(float complex) * BURST_START_SIZE);
        c->burst_buf_size = BURST_START_SIZE;
        c->burst_len = 0;
    } else if (agc_crcf_squelch_get_status(c->agc) == LIQUID_AGC_SQUELCH_TIMEOUT) {
        burst_out->burst = c->burst;
        burst_out->len = c->burst_len;
        burst_out->num = c->burst_num;
        burst_out->freq = c->freq;
        c->burst = NULL;
        c->burst_len = 0;
        c->burst_buf_size = 0;
        ++c->burst_num;
        return 1;
    }

    return 0;
}

void burst_destroy(burst_t *b) {
    free(b->burst);
    free(b->packet.demod);
    free(b->packet.bits);
    b->burst = NULL;
    b->packet.demod = NULL;
    b->packet.bits = NULL;
}
