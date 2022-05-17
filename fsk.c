/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <liquid/liquid.h>

#include "fsk.h"

const unsigned median_symbols = 64; // number of symbols to use for CFO correction
const float max_freq_offset = 0.4f;

unsigned sps(void);

static inline unsigned median_size(void) {
    return sps() * median_symbols;
}

void fsk_demod_init(fsk_demod_t *fsk) {
    fsk->f = freqdem_create(0.8f);
    fsk->pos_points = malloc(sizeof(float) * median_size());
    fsk->neg_points = malloc(sizeof(float) * median_size());
    /*
    fsk->s = symsync_crcf_create_rnyquist(LIQUID_FIRFILT_RRC, samp_rate / sym_rate / channels * 2, 3, 0.8f, 32);
    symsync_crcf_set_lf_bw(fsk->s, 0.045f);
    */
}

void fsk_demod_destroy(fsk_demod_t *fsk) {
    freqdem_destroy(fsk->f);
    free(fsk->pos_points);
    free(fsk->neg_points);
}

static int compare_floats (const void *a, const void *b) {
    float fa = *(const float*) a;
    float fb = *(const float*) b;
    return (fa > fb) - (fa < fb);
}

static int cfo_median(fsk_demod_t *fsk, float *demod, unsigned burst_len, float *cfo_out, float *deviation_out) {
    unsigned i;
    unsigned pos_count = 0, neg_count = 0;
    float midpoint;

    // find the median of the positive and negative points
    for (i = 8; i < 8 + median_size(); ++i) {
        if (fabs(demod[i]) > max_freq_offset)
            return 0;
        if (demod[i] > 0)
            fsk->pos_points[pos_count++] = demod[i];
        else
            fsk->neg_points[neg_count++] = demod[i];
    }

    if (pos_count < median_symbols / 4 || neg_count < median_symbols / 4)
        return 0;

    qsort(fsk->pos_points, pos_count, sizeof(float), compare_floats);
    qsort(fsk->neg_points, neg_count, sizeof(float), compare_floats);

    midpoint = (fsk->pos_points[pos_count*3/4] + fsk->neg_points[neg_count/4])/2.0f;
    if (cfo_out != NULL)
        *cfo_out = midpoint;
    if (deviation_out != NULL)
        *deviation_out = fsk->pos_points[pos_count*3/4] - midpoint;

    return 1;
}

// FIXME move all these constants to the top of the file
unsigned silence_skip(float *demod, unsigned burst_len) {
    unsigned i;
    float first_samples[8];

    for (i = 0; i < 8; ++i)
        // skip the first sample because it tends to be wild
        first_samples[i] = demod[i+1];

    qsort(first_samples, 8, sizeof(float), compare_floats);
    if (first_samples[6] > 0.4) return 0;
    if (first_samples[4] < 0.2) {
        for (i = 2; i < burst_len && fabsf(demod[i]) < 0.1f; ++i)
            ;
        return i;
    }

    return 0;
}

// does the following:
//  fm demodulates burst
//  silence detection
//  carrier frequency offset (CFO) correction
//  normalizes signal to roughly [-1.0f, 1.0f]
//  slices into bits
//
// results stored in p_out->demod and p_out->bits
// if this function fails, it does not touch p_out
void fsk_demod(fsk_demod_t *fsk, float complex *burst, unsigned burst_len, unsigned freq, packet_t *p_out) {
    unsigned i;
    float *demod = NULL;
    float cfo;
    float deviation;
    unsigned silence_offset = 0;

    freqdem_reset(fsk->f);

    if (burst_len < 8 + median_size())
        return;

    demod = malloc(sizeof(float) * burst_len);

    // frequency demodulate
    freqdem_demodulate_block(fsk->f, burst, burst_len, demod);

    if (!cfo_median(fsk, demod, burst_len, &cfo, &deviation)) {
        free(demod);
        return;
    }

    // CFO - carrier frequency offset correction
    for (i = 0; i < burst_len; ++i) {
        demod[i] -= cfo;
        demod[i] /= deviation; // scale to roughly [-1, 1]
    }
    if (fabsf(demod[0]) > 1.5f) demod[0] = 0;
    silence_offset = silence_skip(demod, burst_len);

    uint8_t *bits = malloc((burst_len - silence_offset)/2);
    unsigned len = 0;
    for (i = silence_offset+1; i < burst_len; i+=2) {
        uint8_t bit = demod[i] > 0;
        bits[len++] = bit;
#if 0
        float complex sample, out;
        unsigned int out_len;
        sample = demod[i];
        symsync_crcf_execute(fsk->s, &sample, 1, &out, &out_len);
        if (out_len > 0) {
            // dumb bit decision
            uint8_t bit = creal(out) > 0;
            bits[len++] = bit;
        }
#endif
    }
    p_out->demod = demod;
    p_out->bits = bits;
    p_out->bits_len = len;
    p_out->cfo = cfo;
    p_out->deviation = deviation;
    p_out->silence = silence_offset;
}
