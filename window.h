/*
 * Copyright 2023 ICE9 Consulting LLC
 */

#pragma once

#include <complex.h>

typedef struct _window_t {
    int16_t *r;
    int16_t *i;
    unsigned len;           // length of window
    unsigned m;             // floor(log2(len)) + 1
    unsigned n;             // 2^m
    unsigned mask;          // n-1
    unsigned num_allocated; // number of elements allocated
    unsigned read_index;
} window_t;

void window_init(window_t *w, unsigned n);
void window_push(window_t *w, int8_t *v);
void window_dotprod(window_t *w, int16_t *b, int16_t *out);
