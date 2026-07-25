/*
 * Unit tests for pfbch2.c / pfbch2.h (Polyphase Filterbank Channelizer)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "pfbch2.h"

static void test_pfbch2_init_and_release(void) {
    pfbch2_t pfb;
    unsigned M = 8;
    unsigned m = 4;
    unsigned filter_len = 2 * M * m + 1;
    float *h_float = calloc(filter_len, sizeof(float));

    // simple sinc / lowpass prototype filter coefficients
    for (unsigned i = 0; i < filter_len; i++) {
        h_float[i] = 1.0f / filter_len;
    }

    pfbch2_init(&pfb, M, m, h_float);

    assert(pfb.M == 8);
    assert(pfb.M2 == 4);
    assert(pfb.m == 4);
    assert(pfb.h_len == 64);
    assert(pfb.h_sub_len == 8);
    assert(pfb.h_sub != NULL);
    assert(pfb.w != NULL);

    pfbch2_release(&pfb);
    free(h_float);
    printf("[PASS] test_pfbch2_init_and_release\n");
}

static void test_pfbch2_execute(void) {
    pfbch2_t pfb;
    unsigned M = 8;
    unsigned m = 4;
    unsigned filter_len = 2 * M * m + 1;
    float *h_float = calloc(filter_len, sizeof(float));
    for (unsigned i = 0; i < filter_len; i++) {
        h_float[i] = 0.1f;
    }

    pfbch2_init(&pfb, M, m, h_float);

    // input x: M2=4 complex samples (8 int8_t values)
    int8_t x[8] = { 10, 10, 20, 20, 30, 30, 40, 40 };
    // output y: M=8 complex channel outputs (16 int16_t values)
    int16_t y[16];
    memset(y, 0, sizeof(y));

    pfbch2_execute(&pfb, x, y);

    // execute second frame to push samples through filterbank window
    pfbch2_execute(&pfb, x, y);

    // verify output buffer is updated and non-zero
    int non_zero = 0;
    for (int i = 0; i < 16; i++) {
        if (y[i] != 0) non_zero = 1;
    }
    assert(non_zero == 1);

    pfbch2_release(&pfb);
    free(h_float);
    printf("[PASS] test_pfbch2_execute\n");
}

int main(void) {
    printf("===========================================\n");
    printf(" Running pfbch2.c Unit Tests               \n");
    printf("===========================================\n");
    test_pfbch2_init_and_release();
    test_pfbch2_execute();
    printf("===========================================\n");
    printf(" All pfbch2 tests passed successfully!     \n");
    printf("===========================================\n");
    return 0;
}
