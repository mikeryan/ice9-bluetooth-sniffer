/*
 * Unit tests for fsk.c / fsk.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <complex.h>
#include <string.h>
#include <assert.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <liquid/liquid.h>

#include "fsk.h"

// define sps() stub required by fsk.c
unsigned sps(void) {
    return 2; // 2 samples per symbol
}

extern float comp_ewma(float ewma, float sample);
extern unsigned silence_skip(float *demod, unsigned burst_len);

static void test_fsk_ewma_and_silence(void) {
    float val = comp_ewma(0.0f, 1.0f);
    assert(val > 0.0f);

    float demod[100];
    for (int i = 0; i < 20; i++) demod[i] = 0.01f;
    for (int i = 20; i < 100; i++) demod[i] = 0.9f;

    unsigned skip = silence_skip(demod, 100);
    assert(skip >= 18 && skip <= 22);

    printf("[PASS] test_fsk_ewma_and_silence\n");
}

static void test_fsk_demod_init_destroy(void) {
    fsk_demod_t fsk;
    fsk_demod_init(&fsk);

    assert(fsk.f != NULL);
    assert(fsk.pos_points != NULL);
    assert(fsk.neg_points != NULL);

    fsk_demod_destroy(&fsk);
    printf("[PASS] test_fsk_demod_init_destroy\n");
}

static void test_fsk_demod_synthetic_burst(void) {
    fsk_demod_t fsk;
    fsk_demod_init(&fsk);

    // create a 2-FSK modulated complex sample burst
    unsigned burst_len = 500;
    float complex *burst = malloc(sizeof(float complex) * burst_len);
    float phase = 0.0f;
    float freq_dev = 0.25f; // Normalized frequency deviation

    // generate alternating 0 and 1 symbols (frequency +freq_dev and -freq_dev)
    for (unsigned i = 0; i < burst_len; i++) {
        float f = ((i / 2) % 2 == 0) ? freq_dev : -freq_dev;
        phase += f;
        burst[i] = cosf(phase) + I * sinf(phase);
    }

    packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    fsk_demod(&fsk, burst, burst_len, 2441, &pkt);

    assert(pkt.demod != NULL);
    assert(pkt.bits != NULL);
    assert(pkt.bits_len > 0);

    free(pkt.demod);
    free(pkt.bits);
    free(burst);

    fsk_demod_destroy(&fsk);
    printf("[PASS] test_fsk_demod_synthetic_burst\n");
}

int main(void) {
    printf("===========================================\n");
    printf(" Running fsk.c Unit Tests                  \n");
    printf("===========================================\n");
    test_fsk_ewma_and_silence();
    test_fsk_demod_init_destroy();
    test_fsk_demod_synthetic_burst();
    printf("===========================================\n");
    printf(" All fsk tests passed successfully!        \n");
    printf("===========================================\n");
    return 0;
}
