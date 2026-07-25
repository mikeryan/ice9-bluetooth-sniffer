/*
 * Copyright 2022 ICE9 Consulting LLC
 */

#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libhackrf/hackrf.h>

#include "sdr.h"
#include "options.h"

const unsigned vga_gain = 32;
const unsigned lna_gain = 32;

extern sig_atomic_t running;

void hackrf_list(void) {
    int i;
    char *s;
    hackrf_init();
    hackrf_device_list_t *hackrf_devices = hackrf_device_list();
    for (i = 0; i < hackrf_devices->devicecount; ++i) {
        for (s = hackrf_devices->serial_numbers[i]; *s == '0'; ++s)
            ;
        printf("interface {value=hackrf-%s}{display=ICE9 Bluetooth}\n", s);
    }
    hackrf_device_list_free(hackrf_devices);
}

hackrf_device *hackrf_setup(void) {
    int r;
    hackrf_device *hackrf;

    if (config.samp_rate > 20e6)
        errx(1, "Invalid number of channels for HackRF, must be 20 or fewer");

    hackrf_init();

    if (config.serial == NULL) {
        if ((r = hackrf_open(&hackrf)) != HACKRF_SUCCESS)
            errx(1, "Unable to open HackRF: %s", hackrf_error_name(r));
    } else {
        if ((r = hackrf_open_by_serial(config.serial, &hackrf)) != HACKRF_SUCCESS)
            errx(1, "Unable to open HackRF: %s", hackrf_error_name(r));
    }
    if ((r = hackrf_set_sample_rate(hackrf, config.samp_rate)) != HACKRF_SUCCESS)
        errx(1, "Unable to set HackRF sample rate: %s", hackrf_error_name(r));
    if ((r = hackrf_set_freq(hackrf, config.center_freq * 1e6)) != HACKRF_SUCCESS)
        errx(1, "Unable to set HackRF center frequency: %s", hackrf_error_name(r));
    if ((r = hackrf_set_vga_gain(hackrf, vga_gain)) != HACKRF_SUCCESS)
        errx(1, "Unable to set HackRF VGA gain: %s", hackrf_error_name(r));
    if ((r = hackrf_set_lna_gain(hackrf, lna_gain)) != HACKRF_SUCCESS)
        errx(1, "Unable to set HackRF LNA gain: %s", hackrf_error_name(r));

    return hackrf;
}

int hackrf_rx_cb(hackrf_transfer *t) {
    unsigned i;
    sample_buf_t *s = malloc(sizeof(*s) + t->valid_length * 4);
    s->num = t->valid_length / 2;
    for (i = 0; i < s->num * 2; ++i)
        s->samples[i] = ((int8_t *)t->buffer)[i];
    if (running)
        push_samples(s);
    else
        free(s);
    return 0;
}

static int hackrf_ops_open(sdr_dev_t *dev, const sniffer_config_t *cfg) {
    (void)cfg;
    hackrf_device *hdev = hackrf_setup();
    if (!hdev) return -1;
    dev->priv = hdev;
    return 0;
}

static int hackrf_ops_start(sdr_dev_t *dev) {
    hackrf_device *hdev = (hackrf_device *)dev->priv;
    if (!hdev) return -1;
    int r = hackrf_start_rx(hdev, hackrf_rx_cb, dev);
    if (r == HACKRF_SUCCESS) dev->is_streaming = true;
    return r == HACKRF_SUCCESS ? 0 : -1;
}

static int hackrf_ops_is_streaming(sdr_dev_t *dev) {
    hackrf_device *hdev = (hackrf_device *)dev->priv;
    return (hdev && hackrf_is_streaming(hdev) == HACKRF_TRUE) ? 1 : 0;
}

static int hackrf_ops_stop(sdr_dev_t *dev) {
    hackrf_device *hdev = (hackrf_device *)dev->priv;
    if (hdev) hackrf_stop_rx(hdev);
    dev->is_streaming = false;
    return 0;
}

static void hackrf_ops_close(sdr_dev_t *dev) {
    hackrf_device *hdev = (hackrf_device *)dev->priv;
    if (hdev) {
        hackrf_close(hdev);
        hackrf_exit();
        dev->priv = NULL;
    }
}

const sdr_ops_t hackrf_sdr_ops = {
    .name = "hackrf",
    .open = hackrf_ops_open,
    .start = hackrf_ops_start,
    .is_streaming = hackrf_ops_is_streaming,
    .stop = hackrf_ops_stop,
    .close = hackrf_ops_close,
};
