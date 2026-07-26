/*
 * Unified SDR Hardware Abstraction Layer implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdr.h"

sdr_dev_t *sdr_open_device(const sniffer_config_t *cfg) {
    if (cfg == NULL) return NULL;

    sdr_dev_t *dev = calloc(1, sizeof(sdr_dev_t));
    if (dev == NULL) return NULL;

    if (cfg->bladerf_num >= 0) {
#ifdef HAVE_BLADERF
        dev->ops = &bladerf_sdr_ops;
#else
        fprintf(stderr, "BladeRF support not compiled in\n");
        free(dev);
        return NULL;
#endif
    } else if (cfg->usrp_serial != NULL) {
#ifdef HAVE_UHD
        dev->ops = &usrp_sdr_ops;
#else
        fprintf(stderr, "USRP support not compiled in\n");
        free(dev);
        return NULL;
#endif
    } else if (cfg->serial != NULL) {
#ifdef HAVE_HACKRF
        dev->ops = &hackrf_sdr_ops;
#else
        fprintf(stderr, "HackRF support not compiled in\n");
        free(dev);
        return NULL;
#endif
    } else {
#ifdef HAVE_HACKRF
        dev->ops = &hackrf_sdr_ops;
#elif defined(HAVE_BLADERF)
        dev->ops = &bladerf_sdr_ops;
#elif defined(HAVE_UHD)
        dev->ops = &usrp_sdr_ops;
#else
        fprintf(stderr, "No SDR support compiled in\n");
        free(dev);
        return NULL;
#endif
    }

    if (dev->ops->open && dev->ops->open(dev, cfg) != 0) {
        free(dev);
        return NULL;
    }

    return dev;
}

int sdr_start(sdr_dev_t *dev) {
    if (!dev || !dev->ops || !dev->ops->start) return -1;
    return dev->ops->start(dev);
}

bool sdr_is_streaming(sdr_dev_t *dev) {
    if (!dev || !dev->ops) return false;
    if (dev->ops->is_streaming) {
        return dev->ops->is_streaming(dev);
    }
    return dev->is_streaming;
}

int sdr_stop(sdr_dev_t *dev) {
    if (!dev || !dev->ops || !dev->ops->stop) return -1;
    return dev->ops->stop(dev);
}

void sdr_close(sdr_dev_t *dev) {
    if (!dev) return;
    if (dev->ops && dev->ops->close) {
        dev->ops->close(dev);
    }
    free(dev);
}
