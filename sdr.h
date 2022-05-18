/*
 * Copyright (c) 2022 ICE9 Consulting LLC
 */

#ifndef __SDR_H__
#define __SDR_H__

#include <complex.h>

typedef struct _sample_buf_t {
    unsigned num;
    float complex samples[];
} sample_buf_t;

void push_samples(sample_buf_t *buf);

#endif
