/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#ifndef __FSK_H__
#define __FSK_H__

typedef struct _fsk_demod_t {
    freqdem f;
    symsync_rrrf s;
    float *pos_points;
    float *neg_points;
} fsk_demod_t;

typedef struct _packet_t {
    float *demod;
    unsigned len;
    uint8_t *bits;
    unsigned bits_len;
    unsigned silence;
    float cfo;
    float deviation;
} packet_t;

void fsk_demod_init(fsk_demod_t *fsk);
void fsk_demod_destroy(fsk_demod_t *fsk);
void fsk_demod(fsk_demod_t *fsk, float complex *burst, unsigned burst_len, unsigned freq, packet_t *p_out);

#endif
