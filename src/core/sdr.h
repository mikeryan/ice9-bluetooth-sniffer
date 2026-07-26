/*
 * Copyright (c) 2022 ICE9 Consulting LLC
 */

#ifndef __SDR_H__
#define __SDR_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "options.h"

typedef struct _sample_buf_t {
    unsigned num;
    unsigned sample_size;
    int8_t samples[];
} sample_buf_t;

void push_samples(sample_buf_t *buf);

typedef struct sdr_dev sdr_dev_t;

typedef struct sdr_ops {
    const char *name;
    int (*open)(sdr_dev_t *dev, const sniffer_config_t *cfg);
    int (*start)(sdr_dev_t *dev);
    int (*is_streaming)(sdr_dev_t *dev);
    int (*stop)(sdr_dev_t *dev);
    void (*close)(sdr_dev_t *dev);
} sdr_ops_t;

struct sdr_dev {
    const sdr_ops_t *ops;
    void *priv;
    bool is_streaming;
};

sdr_dev_t *sdr_open_device(const sniffer_config_t *cfg);
int sdr_start(sdr_dev_t *dev);
bool sdr_is_streaming(sdr_dev_t *dev);
int sdr_stop(sdr_dev_t *dev);
void sdr_close(sdr_dev_t *dev);

#ifdef HAVE_HACKRF
extern const sdr_ops_t hackrf_sdr_ops;
#endif
#ifdef HAVE_BLADERF
extern const sdr_ops_t bladerf_sdr_ops;
#endif
#ifdef HAVE_UHD
extern const sdr_ops_t usrp_sdr_ops;
#endif

#endif
