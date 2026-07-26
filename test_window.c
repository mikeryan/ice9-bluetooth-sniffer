/*
 * Unit tests for window.c / window.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "window.h"

static void test_window_init_and_release(void) {
    window_t w;
    window_init(&w, 16);

    assert(w.len == 16);
    assert(w.m == 5);      // floor(log2(16)) + 1 = 4 + 1 = 5
    assert(w.n == 32);     // 2^5 = 32
    assert(w.mask == 31);  // 32 - 1 = 31
    assert(w.num_allocated == 32 + 16 - 1);
    assert(w.r != NULL);
    assert(w.i != NULL);

    window_release(&w);
    printf("[PASS] test_window_init_and_release\n");
}

static void test_window_push(void) {
    window_t w;
    window_init(&w, 4);

    int8_t sample1[2] = { 10, -20 };
    int8_t sample2[2] = { 30, -40 };

    window_push(&w, sample1);
    unsigned idx1 = w.read_index;
    assert(w.r[idx1 + w.len - 1] == (10 << 8));
    assert(w.i[idx1 + w.len - 1] == (-20 << 8));

    window_push(&w, sample2);
    unsigned idx2 = w.read_index;
    assert(w.r[idx2 + w.len - 1] == (30 << 8));
    assert(w.i[idx2 + w.len - 1] == (-40 << 8));

    window_release(&w);
    printf("[PASS] test_window_push\n");
}

static void test_window_dotprod(void) {
    window_t w;
    window_init(&w, 4);

    // Fill window with 4 samples
    int8_t samples[4][2] = {
        { 1, 2 },
        { 3, 4 },
        { 5, 6 },
        { 7, 8 }
    };

    for (int k = 0; k < 4; k++) {
        window_push(&w, samples[k]);
    }

    // Filter coefficients
    int16_t b[4] = { 2, 4, 6, 8 };
    int16_t out[2] = { 0, 0 };

    window_dotprod(&w, b, out);

    // Expected real sum: (1*2 + 3*4 + 5*6 + 7*8) * 256 = (2 + 12 + 30 + 56) * 256 = 100 * 256 = 25600
    // Shifted by 16: 25600 >> 16 = 0
    // Expected imag sum: (2*2 + 4*4 + 6*6 + 8*8) * 256 = (4 + 16 + 36 + 64) * 256 = 120 * 256 = 30720
    // Shifted by 16: 30720 >> 16 = 0

    // With larger coefficients (e.g. 200, 400, 600, 800) to test non-zero output after shift
    int16_t b_large[4] = { 2000, 4000, 6000, 8000 };
    window_dotprod(&w, b_large, out);

    // Expected real sum: (1*2000 + 3*4000 + 5*6000 + 7*8000) * 256 = (2000 + 12000 + 30000 + 56000) * 256 = 100000 * 256 = 25600000
    // 25600000 >> 16 = 390
    assert(out[0] == 390);

    // Expected imag sum: (2*2000 + 4*4000 + 6*6000 + 8*8000) * 256 = (4000 + 16000 + 36000 + 64000) * 256 = 120000 * 256 = 30720000
    // 30720000 >> 16 = 468
    assert(out[1] == 468);

    window_release(&w);
    printf("[PASS] test_window_dotprod\n");
}

int main(void) {
    printf("===========================================\n");
    printf(" Running window.c Unit Tests               \n");
    printf("===========================================\n");
    test_window_init_and_release();
    test_window_push();
    test_window_dotprod();
    printf("===========================================\n");
    printf(" All window tests passed successfully!     \n");
    printf("===========================================\n");
    return 0;
}
