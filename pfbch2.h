/*
 * Copyright 2023 ICE9 Consulting LLC
 */

#pragma once

#include "window.h"

typedef struct _pfbch2_t {
    unsigned M;         // number of channels
    unsigned M2;        // M/2
    unsigned m;         // prototype filter semi-length
    unsigned h_len;     // prototype filter length
    unsigned h_sub_len; // subfilter length

    window_t *w;        // windows for subfilters
    int16_t *h_sub;     // subfilter coefficients
    int flag;           // flag for where to load buffers
} pfbch2_t;

void pfbch2_init(pfbch2_t *c, unsigned M, unsigned m, float *h);
void pfbch2_execute(pfbch2_t *c, int8_t *x, int16_t *y);
