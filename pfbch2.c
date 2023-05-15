/*
 * Copyright 2023 ICE9 Consulting LLC
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pfbch2.h"

void pfbch2_init(pfbch2_t *c, unsigned M, unsigned m, float *h_float) {
    unsigned i, n;
    int16_t *h = malloc((2*m*M+1)*sizeof(int16_t));

    for (i = 0; i < 2 * M * m + 1; ++i)
        h[i] = (int16_t)roundf(h_float[i] * 32768.f);

    memset(c, 0, sizeof(*c));

    c->M = M;
    c->M2 = M/2;
    c->m = m;

    c->h_len = 2 * c->M * c->m;
    c->h_sub_len = 2 * c->m;
    c->h_sub = malloc(sizeof(int16_t) * c->M * c->h_sub_len);
    for (i = 0; i < c->M; ++i) {
        // sub-sample prototype filter, loading coefficients
        // in reverse order
        for (n = 0; n < c->h_sub_len; ++n)
            c->h_sub[i * c->h_sub_len + c->h_sub_len - n - 1] = h[i + n * c->M];
    }

    c->w = malloc(sizeof(window_t) * c->M);
    for (i = 0; i < c->M; ++i)
        window_init(&c->w[i], c->h_sub_len);
}

void pfbch2_execute(pfbch2_t *c, int8_t *x, int16_t *y) {
    unsigned i, offset, cur_offset;
    unsigned base_index = c->flag ? c->M : c->M2;

    for (i = 0; i < c->M2; ++i)
        window_push(&c->w[base_index - i - 1], &x[2*i]);

    offset = c->flag ? c->M2 : 0;
    for (i = 0; i < c->M; ++i) {
        cur_offset = offset + i;
        if (cur_offset >= c->M) cur_offset -= c->M;
        window_dotprod(&c->w[i], &c->h_sub[cur_offset * c->h_sub_len], &y[2*i]);
    }

    c->flag = 1 - c->flag;
}
